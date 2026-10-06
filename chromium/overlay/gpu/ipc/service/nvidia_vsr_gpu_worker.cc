#include "gpu/ipc/service/nvidia_vsr_gpu_worker.h"
#include <array>
#include <dlfcn.h>
#include <cuda.h>
#include <cudaGL.h>
#include "base/memory/raw_ptr.h"
#include "base/sequence_checker.h"
#include "base/scoped_native_library.h"
#include "base/time/time.h"
#include "nvvfx_vsr/gl_bridge.h"
#include "nvvfx_vsr/vsr_processor.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_surface.h"
namespace gpu {
namespace {
bool SameConfig(const nvvfx_vsr::ProcessorConfig& a,
                const nvvfx_vsr::ProcessorConfig& b) {
  return a.input.width==b.input.width && a.input.height==b.input.height &&
         a.output.width==b.output.width && a.output.height==b.output.height &&
         a.quality==b.quality && a.strength==b.strength;
}
}
struct NvidiaVsrGpuWorker::Impl {
  scoped_refptr<gl::GLContext> gl_context;
  scoped_refptr<gl::GLSurface> gl_surface;
  raw_ptr<CUctx_st> cuda_context=nullptr;
  CUdevice device=-1;
  base::ScopedNativeLibrary driver;
  decltype(&cuInit) init=nullptr;
  decltype(&cuGLGetDevices) gl_devices=nullptr;
  decltype(&cuDevicePrimaryCtxRetain) retain=nullptr;
  decltype(&cuDevicePrimaryCtxRelease) release=nullptr;
  decltype(&cuCtxSetCurrent) set_current=nullptr;
  std::array<std::unique_ptr<nvvfx_vsr::GlVsrBridge>,3> bridges;
  std::unique_ptr<nvvfx_vsr::VsrProcessor> processor;
  nvvfx_vsr::ProcessorConfig config{};
  bool quarantined=false;
  SEQUENCE_CHECKER(sequence_checker);
  bool Current(std::string* error) {
    DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker);
    if(quarantined || !gl_context->MakeCurrent(gl_surface.get())) {
      *error="native worker GL context unavailable";return false;
    }
    if(!driver.is_valid()) {
      driver.reset(dlopen("libcuda.so.1",RTLD_NOW|RTLD_LOCAL));
      if(!driver.is_valid()) {*error="CUDA driver unavailable";return false;}
#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)
#define CUDA_LOAD(member,name) member=reinterpret_cast<decltype(member)>(dlsym(driver.get(),STR(name))); \
      if(!member){*error="CUDA symbol unavailable: " #name;return false;}
      CUDA_LOAD(init,cuInit);
      CUDA_LOAD(gl_devices,cuGLGetDevices);
      CUDA_LOAD(retain,cuDevicePrimaryCtxRetain);
      CUDA_LOAD(release,cuDevicePrimaryCtxRelease_v2);
      CUDA_LOAD(set_current,cuCtxSetCurrent);
#undef CUDA_LOAD
#undef STR
#undef STR_IMPL
      unsigned count=0;CUdevice devices[8]{};
      if(init(0)!=CUDA_SUCCESS ||
         gl_devices(&count,devices,8,CU_GL_DEVICE_LIST_ALL)!=CUDA_SUCCESS || count!=1) {
        *error="native GL must identify exactly one CUDA device";return false;
      }
      device=devices[0];CUcontext retained=nullptr;
      if(retain(&retained,device)!=CUDA_SUCCESS) {*error="CUDA primary context unavailable";return false;}
      cuda_context=retained;
    }
    if(!cuda_context || set_current(cuda_context.get())!=CUDA_SUCCESS) {
      *error="CUDA worker context unavailable";return false;
    }
    return true;
  }
};
NvidiaVsrGpuWorker::NvidiaVsrGpuWorker(scoped_refptr<gl::GLContext> context,
                                     scoped_refptr<gl::GLSurface> surface)
    :impl_(std::make_unique<Impl>()) {
  impl_->gl_context=std::move(context);impl_->gl_surface=std::move(surface);
  DETACH_FROM_SEQUENCE(impl_->sequence_checker);
}
NvidiaVsrGpuWorker::~NvidiaVsrGpuWorker() {
  // Explicit Shutdown runs before the owner can delete private textures.
  if(impl_) (void)impl_.release();
}
void NvidiaVsrGpuWorker::ResetSlot(std::size_t slot,
                                    base::OnceCallback<void(bool)> callback) {
  std::string error;
  if(slot<impl_->bridges.size() && !impl_->bridges[slot]) {
    std::move(callback).Run(true);return;
  }
  bool safe=slot<impl_->bridges.size() && impl_->Current(&error);
  if(safe) impl_->bridges[slot].reset();
  else impl_->quarantined=true;
  std::move(callback).Run(safe);
}
void NvidiaVsrGpuWorker::Run(std::size_t slot,nvvfx_vsr::ProcessorConfig cfg,
    GLuint input,GLuint output,GLsync source_fence,base::OnceClosure source_ready,
    base::OnceCallback<bool()> may_start,
    base::OnceCallback<void(NvidiaVsrWorkerResult)> done) {
  NvidiaVsrWorkerResult result;
  auto& p=*impl_;
  // Establish physical completion of the owner's source-copy GL commands
  // before acknowledging decoder-surface release or mapping private textures.
  if(!p.gl_context->MakeCurrent(p.gl_surface.get())) {
    p.quarantined=true;result.quarantine=true;
    result.status="worker GL context lost before source fence";
    std::move(done).Run(std::move(result));return;
  }
  GLenum wait=GL_TIMEOUT_EXPIRED;
  while(wait==GL_TIMEOUT_EXPIRED)
    wait=glClientWaitSync(source_fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000);
  if(wait!=GL_ALREADY_SIGNALED && wait!=GL_CONDITION_SATISFIED) {
    p.quarantined=true;result.quarantine=true;
    result.status="source GL fence failed; retain source access";
    std::move(done).Run(std::move(result));return;
  }
  glDeleteSync(source_fence);
  result.source_ready=true;
  std::move(source_ready).Run();
  if(!p.Current(&result.status)) {
    p.quarantined=true;result.quarantine=true;
    std::move(done).Run(std::move(result));return;
  }
  if(!p.processor || !SameConfig(p.config,cfg)) {
    p.processor.reset();
    p.processor=nvvfx_vsr::VsrProcessor::Create(cfg,NVVFX_VSR_SDK_ROOT,
                                              p.cuda_context.get(),&result.status);
    p.config=cfg;
    if(!p.processor) {
      p.quarantined=true;result.quarantine=true;
      result.status="VFX configuration unavailable: "+result.status;
      std::move(done).Run(std::move(result));return;
    }
  }
  // Initialization/reconfiguration is background work, not a running frame.
  // A frame that aged out while loading a model performs no inference.
  if(!std::move(may_start).Run()) {
    result.status="queue expired or enhancement disabled";
    std::move(done).Run(std::move(result));return;
  }
  if(slot>=p.bridges.size()) {
    result.status="invalid staging slot";
    std::move(done).Run(std::move(result));return;
  }
  if(!p.bridges[slot])
    p.bridges[slot]=nvvfx_vsr::GlVsrBridge::Create(cfg,p.cuda_context.get(),input,
                                                output,&result.status);
  if(!p.bridges[slot] || !p.bridges[slot]->Run(*p.processor,&result.status)) {
    // Conservatively retain context/textures on every bridge failure. This
    // includes partial enqueue failures for which physical completion is unknown.
    p.quarantined=true;result.quarantine=true;
    std::move(done).Run(std::move(result));return;
  }
  result.success=true;result.gpu_ms=p.processor->LastGpuMilliseconds();
  result.status="completed";
  std::move(done).Run(std::move(result));
}
void NvidiaVsrGpuWorker::Shutdown(base::OnceCallback<void(bool)> callback) {
  auto& p=*impl_;
  if(!p.driver.is_valid() && !p.quarantined) {
    impl_.reset();std::move(callback).Run(true);return;
  }
  std::string error;
  if(!p.Current(&error)) {
    (void)impl_.release();std::move(callback).Run(false);return;
  }
  for(auto& bridge:p.bridges) bridge.reset();
  p.processor.reset();
  CUdevice device=p.device;
  bool safe=p.set_current(nullptr)==CUDA_SUCCESS && p.release(device)==CUDA_SUCCESS;
  p.gl_context->ReleaseCurrent(p.gl_surface.get());
  if(safe) {
    p.driver.reset();impl_.reset();
  } else (void)impl_.release();
  std::move(callback).Run(safe);
}
}  // namespace gpu
