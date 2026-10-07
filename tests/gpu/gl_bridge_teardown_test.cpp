#include "nvvfx_vsr/gl_bridge.h"
#include <cuda.h>
#include <nvCVImage.h>
#include <nvVideoEffects.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
bool fail_sync=false;
int unregisters=0,frees=0,image_deallocs=0,effect_destroys=0;
decltype(&cuStreamSynchronize) real_sync=nullptr;
decltype(&cuGraphicsUnregisterResource) real_unregister=nullptr;
decltype(&cuMemFree) real_free=nullptr;
decltype(&NvCVImage_Dealloc) real_dealloc=nullptr;
decltype(&NvVFX_DestroyEffect) real_destroy_effect=nullptr;
void NvCV_API Deallocate(NvCVImage* image) {
  ++image_deallocs;real_dealloc(image);
}
void NvVFX_API DestroyEffect(NvVFX_Handle effect) {
  ++effect_destroys;real_destroy_effect(effect);
}
CUresult CUDAAPI Synchronize(CUstream stream) {
  return fail_sync ? CUDA_ERROR_UNKNOWN : real_sync(stream);
}
CUresult CUDAAPI Unregister(CUgraphicsResource resource) {
  ++unregisters;return real_unregister(resource);
}
CUresult CUDAAPI Free(CUdeviceptr pointer) {
  ++frees;return real_free(pointer);
}
void Check(bool value,const char* message) {
  if(!value)throw std::runtime_error(message);
}
}
// Linker wrapping affects this fixture's linked core only. Signatures come
// from official CUDA headers; the actual CUDA driver serves every other call.
extern "C" void* __real_dlsym(void*,const char*);
extern "C" void* __wrap_dlsym(void* handle,const char* symbol) {
  void* actual=__real_dlsym(handle,symbol);
  if(!std::strcmp(symbol,"cuStreamSynchronize")) {
    real_sync=reinterpret_cast<decltype(real_sync)>(actual);
    return reinterpret_cast<void*>(&Synchronize);
  }
  if(!std::strcmp(symbol,"cuGraphicsUnregisterResource")) {
    real_unregister=reinterpret_cast<decltype(real_unregister)>(actual);
    return reinterpret_cast<void*>(&Unregister);
  }
  if(!std::strcmp(symbol,"cuMemFree_v2")) {
    real_free=reinterpret_cast<decltype(real_free)>(actual);
    return reinterpret_cast<void*>(&Free);
  }
  if(!std::strcmp(symbol,"NvCVImage_Dealloc")) {
    real_dealloc=reinterpret_cast<decltype(real_dealloc)>(actual);
    return reinterpret_cast<void*>(&Deallocate);
  }
  if(!std::strcmp(symbol,"NvVFX_DestroyEffect")) {
    real_destroy_effect=reinterpret_cast<decltype(real_destroy_effect)>(actual);
    return reinterpret_cast<void*>(&DestroyEffect);
  }
  return actual;
}
int main(int argc,char** argv){try {
  Check(argc==2,"SDK root required");
  EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major,minor;Check(eglInitialize(display,&major,&minor),"EGL initialize");
  Check(eglBindAPI(EGL_OPENGL_ES_API),"EGL GLES API");
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,
      EGL_OPENGL_ES3_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,
      EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config;EGLint count;
  Check(eglChooseConfig(display,attrs,&config,1,&count)&&count,"EGL config");
  EGLint surface_attrs[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  auto surface=eglCreatePbufferSurface(display,config,surface_attrs);
  auto context=eglCreateContext(display,config,EGL_NO_CONTEXT,context_attrs);
  Check(eglMakeCurrent(display,surface,surface,context),"GL current");
  Check(cuInit(0)==CUDA_SUCCESS,"CUDA init");
  CUdevice device;CUcontext cuda_context;
  Check(cuDeviceGet(&device,0)==CUDA_SUCCESS,"CUDA device");
  Check(cuDevicePrimaryCtxRetain(&cuda_context,device)==CUDA_SUCCESS,"CUDA retain");
  Check(cuCtxSetCurrent(cuda_context)==CUDA_SUCCESS,"CUDA current");
  GLuint textures[2];glGenTextures(2,textures);
  for(int i=0;i<2;++i) {
    glBindTexture(GL_TEXTURE_2D,textures[i]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,i?16:8,i?16:8,0,
                 GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
  }
  std::string error;
  auto bridge=nvvfx_vsr::GlVsrBridge::Create({{8,8},{16,16},4,1},cuda_context,
                                          textures[0],textures[1],&error);
  Check(bool(bridge),error.c_str());
  auto processor=nvvfx_vsr::VsrProcessor::Create(
      {{1280,720},{1920,1080},4,1},argv[1],cuda_context,&error);
  Check(bool(processor),error.c_str());
  fail_sync=true;
  Check(!processor->Close(&error),"failed VFX drain reported safe context release");
  processor.reset();
  Check(image_deallocs==0 && effect_destroys==0,"unsafe VFX teardown freed SDK resources");
  Check(!bridge->Close(&error),"failed sync reported safe texture deletion");
  Check(unregisters==0 && frees==0,"failed sync released CUDA resources");
  bridge.reset();
  Check(unregisters==0 && frees==0,"destructor freed quarantined resources");
  // Texture/context owners must keep their resources after Close fails. The
  // OS/driver reclaims this small deliberately quarantined fixture at exit.
  std::cout<<"failed bridge teardown retains CUDA/GL resources: passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
