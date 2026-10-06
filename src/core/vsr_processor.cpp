#include "nvvfx_vsr/vsr_processor.h"
#include <cuda.h>
#include <nvCVImage.h>
#include <nvVideoEffects.h>
#include <nvVFXVideoSuperRes.h>
#include <dlfcn.h>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>
extern "C" NvCVImage* nvvfx_create_image_descriptor();
extern "C" void nvvfx_free_image_descriptor(NvCVImage*);
#define STRINGIFY_IMPL(x) #x
#define STRINGIFY(x) STRINGIFY_IMPL(x)
namespace nvvfx_vsr {
namespace {
void Error(std::string* out, const std::string& text) { if(out) *out=text; }
template<class T> T Symbol(void* library,const char* name) {
  auto value=reinterpret_cast<T>(dlsym(library,name));
  if(!value) throw std::runtime_error(std::string("missing runtime symbol: ")+name);
  return value;
}
}
struct VsrProcessor::Impl {
  ProcessorConfig config;
  CUcontext context=nullptr;
  CUstream stream=nullptr;
  CUevent start=nullptr,done=nullptr;
  NvVFX_Handle effect=nullptr;
  NvCVImage* input=nullptr;
  NvCVImage* output=nullptr;
  std::vector<void*> libraries;
  bool busy=false,failed=false;
  float last_ms=0;
#define FN(name) decltype(&name) name##_fn=nullptr
  FN(cuCtxGetCurrent); FN(cuCtxPushCurrent); FN(cuCtxPopCurrent);
  FN(cuStreamCreate); FN(cuStreamDestroy); FN(cuStreamSynchronize);
  FN(cuEventCreate); FN(cuEventDestroy); FN(cuEventRecord); FN(cuEventQuery);
  FN(cuEventElapsedTime); FN(cuMemcpy2DAsync);
  FN(NvVFX_GetVersion); FN(NvVFX_CreateEffect); FN(NvVFX_DestroyEffect);
  FN(NvVFX_SetImage); FN(NvVFX_SetCudaStream); FN(NvVFX_SetU32);
  FN(NvVFX_SetF32); FN(NvVFX_Load); FN(NvVFX_Run);
  FN(NvCVImage_Alloc); FN(NvCVImage_Dealloc);
#undef FN
  void* Open(const std::string& path) {
    void* h=dlopen(path.c_str(),RTLD_NOW|RTLD_GLOBAL);
    if(!h) throw std::runtime_error("cannot load "+path+": "+dlerror());
    libraries.push_back(h); return h;
  }
  void LoadRuntime(const std::string& root) {
    void* cuda=Open("libcuda.so.1");
#define CUDA(name) name##_fn=Symbol<decltype(name##_fn)>(cuda,STRINGIFY(name))
    CUDA(cuCtxGetCurrent); CUDA(cuCtxPushCurrent); CUDA(cuCtxPopCurrent);
    CUDA(cuStreamCreate); CUDA(cuStreamDestroy); CUDA(cuStreamSynchronize);
    CUDA(cuEventCreate); CUDA(cuEventDestroy); CUDA(cuEventRecord);
    CUDA(cuEventQuery); CUDA(cuEventElapsedTime); CUDA(cuMemcpy2DAsync);
#undef CUDA
    for(auto name:{"libcudart.so.12","libnppc.so.12","libnppial.so.12",
      "libnppicc.so.12","libnppidei.so.12","libnppig.so.12","libcudnn.so.9"})
      Open(root+"/external/cuda/lib/"+name);
    void* image=Open(root+"/lib/libNVCVImage.so");
    NvCVImage_Alloc_fn=Symbol<decltype(NvCVImage_Alloc_fn)>(image,"NvCVImage_Alloc");
    NvCVImage_Dealloc_fn=Symbol<decltype(NvCVImage_Dealloc_fn)>(image,"NvCVImage_Dealloc");
    Open(root+"/lib/libVideoFXLocal.so");
    Open(root+"/lib/libnvngxruntime.so");
    Open(root+"/features/nvvfxvideosuperres/lib/libnvVFXVideoSuperRes.so");
    void* vfx=Open(root+"/lib/libVideoFX.so");
#define VFX(name) name##_fn=Symbol<decltype(name##_fn)>(vfx,#name)
    VFX(NvVFX_GetVersion); VFX(NvVFX_CreateEffect); VFX(NvVFX_DestroyEffect);
    VFX(NvVFX_SetImage); VFX(NvVFX_SetCudaStream); VFX(NvVFX_SetU32);
    VFX(NvVFX_SetF32); VFX(NvVFX_Load); VFX(NvVFX_Run);
#undef VFX
  }
  bool Current(std::string* error) {
    CUcontext actual=nullptr;
    if(!context || cuCtxGetCurrent_fn(&actual)!=CUDA_SUCCESS || actual!=context) {
      Error(error,"processor requires its CUDA context on the calling worker");return false;
    }
    return true;
  }
  bool Drain(std::string* error) {
    if(!stream) return true;
    if(!Current(error)) return false;
    CUresult r=cuStreamSynchronize_fn(stream);
    if(r!=CUDA_SUCCESS) { failed=true;Error(error,"CUDA drain failed: "+std::to_string(r));return false; }
    if(busy && !failed && cuEventElapsedTime_fn(&last_ms,start,done)!=CUDA_SUCCESS) {
      failed=true;Error(error,"CUDA timing event failed");return false;
    }
    busy=false; return true;
  }
  void ReleaseGpuResources() {
    // Caller only releases resources after a successful drain, including partial init.
    if(effect && NvVFX_DestroyEffect_fn) NvVFX_DestroyEffect_fn(effect);
    effect=nullptr;
    for(auto image:{output,input}) if(image) {
      if(NvCVImage_Dealloc_fn) NvCVImage_Dealloc_fn(image);
      nvvfx_free_image_descriptor(image);
    }
    input=output=nullptr;
    if(done) cuEventDestroy_fn(done);
    if(start) cuEventDestroy_fn(start);
    if(stream) cuStreamDestroy_fn(stream);
    done=start=nullptr;stream=nullptr;
  }
  ~Impl() {
    ReleaseGpuResources();
    for(auto it=libraries.rbegin();it!=libraries.rend();++it) dlclose(*it);
  }
};
bool VsrProcessor::PreloadRuntime(const std::string& root,std::string* error) {
  if(root.empty() || root[0]!='/') {
    Error(error,"absolute SDK root required");return false;
  }
  auto impl=std::make_unique<Impl>();
  try {
    impl->LoadRuntime(root);
    unsigned version=0;
    if(impl->NvVFX_GetVersion_fn(&version)!=NVCV_SUCCESS ||
       version!=((1U<<24)|(3U<<16)))
      throw std::runtime_error("VFX 1.3.0.0 required");
    // Avoid unloading before sandboxed worker initialization. No CUDA stream,
    // image, effect, or worker was created; modules live until process exit.
    (void)impl.release();
    return true;
  } catch(const std::exception& e) { Error(error,e.what());return false; }
}
VsrProcessor::VsrProcessor(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::unique_ptr<VsrProcessor> VsrProcessor::Create(const ProcessorConfig& config,
    const std::string& root,CUcontext context,std::string* error) {
  if(!ValidProcessorConfig(config) || !context || root.empty() || root[0]!='/') {
    Error(error,"valid config, absolute SDK root and current CUDA context required");return nullptr;
  }
  auto impl=std::make_unique<Impl>();impl->config=config;impl->context=context;
  try {
    impl->LoadRuntime(root);
    if(!impl->Current(error)) return nullptr;
    auto cuda=[](CUresult r) {if(r!=CUDA_SUCCESS) throw std::runtime_error("CUDA status "+std::to_string(r));};
    auto vfx=[](NvCV_Status r) {if(r!=NVCV_SUCCESS) throw std::runtime_error("VFX status "+std::to_string(r));};
    unsigned version=0;vfx(impl->NvVFX_GetVersion_fn(&version));
    if(version!=((1U<<24)|(3U<<16))) throw std::runtime_error("VFX 1.3.0.0 required");
    cuda(impl->cuStreamCreate_fn(&impl->stream,CU_STREAM_NON_BLOCKING));
    cuda(impl->cuEventCreate_fn(&impl->start,CU_EVENT_DEFAULT));
    cuda(impl->cuEventCreate_fn(&impl->done,CU_EVENT_DEFAULT));
    impl->input=nvvfx_create_image_descriptor();impl->output=nvvfx_create_image_descriptor();
    if(!impl->input || !impl->output) throw std::runtime_error("descriptor allocation failed");
    vfx(impl->NvCVImage_Alloc_fn(impl->input,config.input.width,config.input.height,
      NVCV_RGBA,NVCV_U8,NVCV_INTERLEAVED,NVCV_GPU,32));
    vfx(impl->NvCVImage_Alloc_fn(impl->output,config.output.width,config.output.height,
      NVCV_RGBA,NVCV_U8,NVCV_INTERLEAVED,NVCV_GPU,32));
    vfx(impl->NvVFX_CreateEffect_fn(NVVFX_FX_VIDEO_SUPER_RES,&impl->effect));
    vfx(impl->NvVFX_SetImage_fn(impl->effect,NVVFX_INPUT_IMAGE,impl->input));
    vfx(impl->NvVFX_SetImage_fn(impl->effect,NVVFX_OUTPUT_IMAGE,impl->output));
    vfx(impl->NvVFX_SetCudaStream_fn(impl->effect,NVVFX_CUDA_STREAM,impl->stream));
    vfx(impl->NvVFX_SetU32_fn(impl->effect,NVVFX_QUALITY_LEVEL,config.quality));
    vfx(impl->NvVFX_SetF32_fn(impl->effect,NVVFX_STRENGTH,config.strength));
    vfx(impl->NvVFX_Load_fn(impl->effect));
    return std::unique_ptr<VsrProcessor>(new VsrProcessor(std::move(impl)));
  } catch(const std::exception& e) {
    Error(error,e.what());
    if(impl->stream && !impl->Drain(nullptr)) (void)impl.release();
    return nullptr;
  }
}
VsrProcessor::~VsrProcessor() {
  if(!impl_) return;
  bool pushed=false;
  if(!impl_->Current(nullptr) && impl_->context && impl_->cuCtxPushCurrent_fn) {
    if(impl_->cuCtxPushCurrent_fn(impl_->context)==CUDA_SUCCESS) pushed=true;
    else { (void)impl_.release();return; }
  }
  auto pop=impl_->cuCtxPopCurrent_fn;
  if(!impl_->Drain(nullptr)) {
    (void)impl_.release();
    if(pushed) { CUcontext old=nullptr;(void)pop(&old); }
    return;
  }
  impl_->ReleaseGpuResources();
  if(pushed) { CUcontext old=nullptr;(void)pop(&old); }
  impl_.reset();
}
bool VsrProcessor::Submit(CudaFrameView in,CudaFrameView out,std::string* error) {
  auto& p=*impl_;
  if(p.failed || p.busy) {Error(error,p.failed?"processor failed":"processor busy");return false;}
  auto valid=[](CudaFrameView view,Dimensions size) {
    return view.device_pointer && view.size.width==size.width && view.size.height==size.height &&
      view.pitch>=std::size_t(size.width)*4 && view.pitch<=std::numeric_limits<int>::max();
  };
  if(!valid(in,p.config.input)||!valid(out,p.config.output)) {Error(error,"invalid RGBA8 frame view");return false;}
  if(!p.Current(error)) return false;
  p.busy=true;
  auto check=[&](int status,const char* stage) {
    if(status==0) return true;
    p.failed=true;Error(error,std::string(stage)+" status "+std::to_string(status));return false;
  };
  if(!check(p.cuEventRecord_fn(p.start,p.stream),"start event")) return false;
  CUDA_MEMCPY2D copy{};
  copy.srcMemoryType=CU_MEMORYTYPE_DEVICE;copy.srcDevice=in.device_pointer;copy.srcPitch=in.pitch;
  copy.dstMemoryType=CU_MEMORYTYPE_DEVICE;copy.dstDevice=reinterpret_cast<CUdeviceptr>(p.input->pixels);
  copy.dstPitch=p.input->pitch;copy.WidthInBytes=std::size_t(in.size.width)*4;copy.Height=in.size.height;
  if(!check(p.cuMemcpy2DAsync_fn(&copy,p.stream),"input copy")) return false;
  if(!check(p.NvVFX_Run_fn(p.effect,1),"VFX Run")) return false;
  copy.srcDevice=reinterpret_cast<CUdeviceptr>(p.output->pixels);copy.srcPitch=p.output->pitch;
  copy.dstDevice=out.device_pointer;copy.dstPitch=out.pitch;
  copy.WidthInBytes=std::size_t(out.size.width)*4;copy.Height=out.size.height;
  if(!check(p.cuMemcpy2DAsync_fn(&copy,p.stream),"output copy")) return false;
  return check(p.cuEventRecord_fn(p.done,p.stream),"completion event");
}
ProcessState VsrProcessor::Poll(std::string* error) {
  auto& p=*impl_;
  if(p.failed || !p.Current(error)) return ProcessState::Failed;
  if(!p.busy) return ProcessState::Ready;
  CUresult r=p.cuEventQuery_fn(p.done);
  if(r==CUDA_ERROR_NOT_READY) return ProcessState::Pending;
  if(r!=CUDA_SUCCESS) {p.failed=true;Error(error,"CUDA event status "+std::to_string(r));return ProcessState::Failed;}
  if(p.cuEventElapsedTime_fn(&p.last_ms,p.start,p.done)!=CUDA_SUCCESS) {
    p.failed=true;Error(error,"CUDA timing event failed");return ProcessState::Failed;
  }
  p.busy=false;return ProcessState::Ready;
}
bool VsrProcessor::Drain(std::string* error) { return impl_->Drain(error); }
float VsrProcessor::LastGpuMilliseconds() const {return impl_->last_ms;}
}
