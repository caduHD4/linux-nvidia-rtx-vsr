#include "nvvfx_vsr/gl_bridge.h"
#include <cuda.h>
#include <cudaGL.h>
#include <dlfcn.h>
#include <stdexcept>
#include <utility>
#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)
namespace nvvfx_vsr {
struct GlVsrBridge::Impl {
  ProcessorConfig config;
  CUcontext context=nullptr;
  void* library=nullptr;
  CUgraphicsResource resources[2]{};
  CUdeviceptr linear[2]{};
  std::size_t pitches[2]{};
  CUstream stream=nullptr;
  bool mapped=false,failed=false;
#define FN(x) decltype(&x) x##_fn=nullptr
  FN(cuCtxGetCurrent); FN(cuCtxGetDevice); FN(cuGLGetDevices);
  FN(cuCtxPushCurrent); FN(cuCtxPopCurrent);
  FN(cuStreamCreate); FN(cuStreamDestroy); FN(cuStreamSynchronize);
  FN(cuMemAllocPitch); FN(cuMemFree); FN(cuMemcpy2DAsync);
  FN(cuGraphicsGLRegisterImage); FN(cuGraphicsUnregisterResource);
  FN(cuGraphicsMapResources); FN(cuGraphicsUnmapResources);
  FN(cuGraphicsSubResourceGetMappedArray); FN(cuArrayGetDescriptor);
#undef FN
  void Load() {
    library=dlopen("libcuda.so.1",RTLD_NOW|RTLD_LOCAL);
    if(!library) throw std::runtime_error("CUDA driver unavailable");
#define LOAD(x) x##_fn=reinterpret_cast<decltype(x##_fn)>(dlsym(library,STR(x))); \
  if(!x##_fn) throw std::runtime_error("CUDA symbol unavailable: " STR(x))
    LOAD(cuCtxGetCurrent); LOAD(cuCtxGetDevice); LOAD(cuGLGetDevices);
    LOAD(cuCtxPushCurrent); LOAD(cuCtxPopCurrent);
    LOAD(cuStreamCreate); LOAD(cuStreamDestroy); LOAD(cuStreamSynchronize);
    LOAD(cuMemAllocPitch); LOAD(cuMemFree); LOAD(cuMemcpy2DAsync);
    LOAD(cuGraphicsGLRegisterImage); LOAD(cuGraphicsUnregisterResource);
    LOAD(cuGraphicsMapResources); LOAD(cuGraphicsUnmapResources);
    LOAD(cuGraphicsSubResourceGetMappedArray); LOAD(cuArrayGetDescriptor);
#undef LOAD
  }
  bool Current() const {
    CUcontext actual=nullptr;return context&&cuCtxGetCurrent_fn&&
      cuCtxGetCurrent_fn(&actual)==CUDA_SUCCESS&&actual==context;
  }
  bool Synchronize() {
    return !stream || cuStreamSynchronize_fn(stream)==CUDA_SUCCESS;
  }
  bool ReleaseGpuResources() {
    if(mapped) {
      if(cuGraphicsUnmapResources_fn(2,resources,stream)!=CUDA_SUCCESS ||
         !Synchronize()) return false;
      mapped=false;
    }
    for(auto& r:resources) if(r) {
      if(cuGraphicsUnregisterResource_fn(r)!=CUDA_SUCCESS) return false;
      r=nullptr;
    }
    for(auto& ptr:linear) if(ptr) {
      if(cuMemFree_fn(ptr)!=CUDA_SUCCESS) return false;
      ptr=0;
    }
    if(stream) {
      if(cuStreamDestroy_fn(stream)!=CUDA_SUCCESS) return false;
      stream=nullptr;
    }
    return true;
  }
  ~Impl(){ReleaseGpuResources();if(library) dlclose(library);}

};
GlVsrBridge::GlVsrBridge(std::unique_ptr<Impl> impl):impl_(std::move(impl)){}
std::unique_ptr<GlVsrBridge> GlVsrBridge::Create(ProcessorConfig cfg,CUcontext context,
    unsigned input,unsigned output,std::string* error) {
  auto p=std::make_unique<Impl>();p->config=cfg;p->context=context;
  try {
    if(!ValidProcessorConfig(cfg)||!context||!input||!output||input==output)
      throw std::runtime_error("invalid private GL texture configuration");
    p->Load();if(!p->Current())throw std::runtime_error("worker CUDA context not current");
    auto check=[](CUresult r){if(r!=CUDA_SUCCESS)throw std::runtime_error("CUDA GL status "+std::to_string(r));};
    CUdevice context_device=-1,gl_devices[8]{};unsigned count=0;
    check(p->cuCtxGetDevice_fn(&context_device));
    check(p->cuGLGetDevices_fn(&count,gl_devices,8,CU_GL_DEVICE_LIST_ALL));
    bool matched=false;for(unsigned i=0;i<count;++i)matched|=gl_devices[i]==context_device;
    if(!matched)throw std::runtime_error("GL and CUDA devices differ");
    check(p->cuStreamCreate_fn(&p->stream,CU_STREAM_NON_BLOCKING));
    check(p->cuGraphicsGLRegisterImage_fn(&p->resources[0],input,0x0DE1,CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY));
    check(p->cuGraphicsGLRegisterImage_fn(&p->resources[1],output,0x0DE1,CU_GRAPHICS_REGISTER_FLAGS_WRITE_DISCARD));
    for(int i=0;i<2;++i){auto size=i?cfg.output:cfg.input;
      check(p->cuMemAllocPitch_fn(&p->linear[i],&p->pitches[i],std::size_t(size.width)*4,size.height,4));}
    return std::unique_ptr<GlVsrBridge>(new GlVsrBridge(std::move(p)));
  }catch(const std::exception& e){if(error)*error=e.what();return nullptr;}
}
bool GlVsrBridge::Run(VsrProcessor& processor,std::string* error) {
  auto& p=*impl_;
  if(p.failed||!p.Current()){if(error)*error="GL bridge unavailable or context mismatch";return false;}
  try {
    auto check=[](CUresult r){if(r!=CUDA_SUCCESS)throw std::runtime_error("CUDA bridge status "+std::to_string(r));};
    check(p.cuGraphicsMapResources_fn(2,p.resources,p.stream));p.mapped=true;
    CUarray arrays[2]{};
    for(int i=0;i<2;++i){
      check(p.cuGraphicsSubResourceGetMappedArray_fn(&arrays[i],p.resources[i],0,0));
      CUDA_ARRAY_DESCRIPTOR desc{};check(p.cuArrayGetDescriptor_fn(&desc,arrays[i]));
      auto size=i?p.config.output:p.config.input;
      if(desc.Width!=std::size_t(size.width)||desc.Height!=std::size_t(size.height)||
         desc.NumChannels!=4||desc.Format!=CU_AD_FORMAT_UNSIGNED_INT8)
        throw std::runtime_error("private texture must be sized RGBA8");
    }
    CUDA_MEMCPY2D copy{};
    copy.srcMemoryType=CU_MEMORYTYPE_ARRAY;copy.srcArray=arrays[0];
    copy.dstMemoryType=CU_MEMORYTYPE_DEVICE;copy.dstDevice=p.linear[0];copy.dstPitch=p.pitches[0];
    copy.WidthInBytes=std::size_t(p.config.input.width)*4;copy.Height=p.config.input.height;
    check(p.cuMemcpy2DAsync_fn(&copy,p.stream));check(p.cuStreamSynchronize_fn(p.stream));
    std::string detail;
    if(!processor.Submit({p.linear[0],p.pitches[0],p.config.input},
                        {p.linear[1],p.pitches[1],p.config.output},&detail)) {
      // Submit can fail after a partial enqueue; borrowed linear buffers stay
      // alive unless the processor stream has drained successfully.
      if(!processor.Drain(nullptr)){p.failed=true;throw std::runtime_error("VFX partial enqueue could not drain");}
      throw std::runtime_error(detail);
    }
    if(!processor.Drain(&detail)){p.failed=true;throw std::runtime_error(detail);}
    copy.srcMemoryType=CU_MEMORYTYPE_DEVICE;copy.srcDevice=p.linear[1];copy.srcPitch=p.pitches[1];
    copy.dstMemoryType=CU_MEMORYTYPE_ARRAY;copy.dstArray=arrays[1];copy.dstPitch=0;
    copy.WidthInBytes=std::size_t(p.config.output.width)*4;copy.Height=p.config.output.height;
    check(p.cuMemcpy2DAsync_fn(&copy,p.stream));
    check(p.cuGraphicsUnmapResources_fn(2,p.resources,p.stream));p.mapped=false;
    check(p.cuStreamSynchronize_fn(p.stream));return true;
  }catch(const std::exception& e){
    if(error)*error=e.what();
    // On a processor drain failure its stream can still reference our linear
    // buffers. Do not free them or unregister graphics resources at teardown.
    if(p.failed)return false;
    if(!p.Synchronize()){p.failed=true;return false;}
    if(p.mapped){
      if(p.cuGraphicsUnmapResources_fn(2,p.resources,p.stream)!=CUDA_SUCCESS||!p.Synchronize())
        p.failed=true;
      else p.mapped=false;
    }
    return false;
  }
}
bool GlVsrBridge::Close(std::string* error) {
  if(!impl_) return true;
  auto fail=[&] {
    impl_->failed=true;
    if(error) *error="CUDA GL bridge teardown could not complete safely";
    return false;
  };
  if(impl_->failed) return fail();
  bool pushed=false;
  if(!impl_->Current()) {
    if(impl_->cuCtxPushCurrent_fn(impl_->context)!=CUDA_SUCCESS) return fail();
    pushed=true;
  }
  auto pop=impl_->cuCtxPopCurrent_fn;
  bool safe=impl_->Synchronize() && impl_->ReleaseGpuResources();
  if(pushed) {
    CUcontext previous=nullptr;
    safe=(pop(&previous)==CUDA_SUCCESS) && safe;
  }
  if(!safe) return fail();
  impl_.reset();return true;
}
GlVsrBridge::~GlVsrBridge(){
  if(!Close(nullptr)) (void)impl_.release();
}
}
