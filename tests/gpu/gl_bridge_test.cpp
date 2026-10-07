#define GL_GLEXT_PROTOTYPES
#include "nvvfx_vsr/gl_bridge.h"
#include <cuda.h>
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <iostream>
#include <cstdlib>
#include <thread>
#include <exception>
#include <stdexcept>
#include <vector>
using namespace nvvfx_vsr;
void Check(bool value,const std::string& text) {if(!value) throw std::runtime_error(text);}
int main(int argc,char** argv){try{
  Check(argc==2,"SDK root required");
  EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major=0,minor=0;Check(eglInitialize(display,&major,&minor),"EGL initialize failed");
  bool gles=std::getenv("NVVFX_TEST_GLES");
  Check(eglBindAPI(gles?EGL_OPENGL_ES_API:EGL_OPENGL_API),"OpenGL API unavailable");
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,gles?EGL_OPENGL_ES3_BIT:EGL_OPENGL_BIT,
    EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
  EGLConfig config;EGLint count;Check(eglChooseConfig(display,attrs,&config,1,&count)&&count,"EGL config");
  EGLint pbattrs[]={EGL_WIDTH,16,EGL_HEIGHT,16,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,pbattrs);
  EGLint context_attrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,gles?context_attrs:nullptr);
  Check(eglMakeCurrent(display,surface,surface,context),"EGL make current");
  std::cout<<"GL renderer="<<glGetString(GL_RENDERER)<<'\n';
  Check(cuInit(0)==CUDA_SUCCESS,"CUDA init");CUdevice device;CUcontext cuda_context;
  Check(cuDeviceGet(&device,0)==CUDA_SUCCESS,"CUDA device");
  Check(cuDevicePrimaryCtxRetain(&cuda_context,device)==CUDA_SUCCESS,"CUDA context");
  Check(cuCtxSetCurrent(cuda_context)==CUDA_SUCCESS,"CUDA current");
  for(auto cfg:{ProcessorConfig{{1280,720},{1920,1080},4,1},
                ProcessorConfig{{1920,1080},{1920,1080},11,1}}){
    GLuint textures[2];glGenTextures(2,textures);
    std::vector<unsigned char> input(std::size_t(cfg.input.width)*cfg.input.height*4);
    for(std::size_t p=0;p<input.size();p+=4){input[p]=64;input[p+1]=128;input[p+2]=192;input[p+3]=255;}
    glBindTexture(GL_TEXTURE_2D,textures[0]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,cfg.input.width,cfg.input.height,0,GL_RGBA,GL_UNSIGNED_BYTE,input.data());
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,cfg.output.width,cfg.output.height,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    // Tests may wait on a fixture fence; this is a private worker, not compositor.
    auto fence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    GLenum waited=glClientWaitSync(fence,GL_SYNC_FLUSH_COMMANDS_BIT,1000000000);
    Check(waited==GL_ALREADY_SIGNALED||waited==GL_CONDITION_SATISFIED,"fixture GL fence");glDeleteSync(fence);
    std::string error;
    EGLContext worker_context=eglCreateContext(display,config,context,gles?context_attrs:nullptr);
    Check(worker_context!=EGL_NO_CONTEXT,"shared worker EGL context");
    EGLSurface worker_surface=eglCreatePbufferSurface(display,config,pbattrs);
    std::exception_ptr worker_error;
    std::thread worker([&]{try{
      Check(eglMakeCurrent(display,worker_surface,worker_surface,worker_context),"worker GL current");
      Check(cuCtxSetCurrent(cuda_context)==CUDA_SUCCESS,"worker CUDA current");
      auto processor=VsrProcessor::Create(cfg,argv[1],cuda_context,&error);Check(bool(processor),error);
      auto bridge=GlVsrBridge::Create(cfg,cuda_context,textures[0],textures[1],&error);Check(bool(bridge),error);
      // Changing colors detects first-frame corruption and a one-frame lag.
      // Pixel uploads/readbacks below belong only to this diagnostic fixture.
      const unsigned char colors[3][3]={{64,128,192},{192,64,128},{128,192,64}};
      for(int i=0;i<31;++i) {
        const auto& expected=colors[i%3];
        for(std::size_t x=0;x<input.size();x+=4)
          for(int c=0;c<3;++c) input[x+c]=expected[c];
        glBindTexture(GL_TEXTURE_2D,textures[0]);
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,cfg.input.width,cfg.input.height,
                       GL_RGBA,GL_UNSIGNED_BYTE,input.data());
        glFinish();
        Check(bridge->Run(*processor,&error),error);
        GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
        Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"per-frame FBO incomplete");
        unsigned char pixel[4]{};
        glReadPixels(cfg.output.width/2,cfg.output.height/2,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel);
        for(int c=0;c<3;++c)
          Check(std::abs(int(pixel[c])-int(expected[c]))<=30,
                "GL output does not match current frame "+std::to_string(i));
        Check(pixel[3]==255,"GL current-frame alpha invalid");
        glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);
      }
      Check(bridge->Close(&error),error);
      Check(bridge->Close(&error),"bridge close must be idempotent");
      bridge.reset();processor.reset();
      eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    }catch(...){worker_error=std::current_exception();}});
    worker.join();if(worker_error)std::rethrow_exception(worker_error);
    eglDestroyContext(display,worker_context);eglDestroySurface(display,worker_surface);
    glBindTexture(GL_TEXTURE_2D,textures[1]);
    std::vector<unsigned char> output(std::size_t(cfg.output.width)*cfg.output.height*4);
    if(gles){
      GLuint fbo;glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
      glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,textures[1],0);
      Check(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE,"output FBO incomplete");
      glReadPixels(0,0,cfg.output.width,cfg.output.height,GL_RGBA,GL_UNSIGNED_BYTE,output.data());
      glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);
    } else glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,output.data()); // Diagnostic-only.
    auto p=(std::size_t(cfg.output.height/2)*cfg.output.width+cfg.output.width/2)*4;
    Check(output[p]>20&&output[p]<100&&output[p+1]>90&&output[p+1]<165&&
          output[p+2]>150&&output[p+2]<225&&output[p+3]==255,"GL output channels/alpha invalid");
    Check(glGetError()==GL_NO_ERROR,"GL error");
    glDeleteTextures(2,textures);
    std::cout<<"GL->CUDA->VFX->GL mode="<<cfg.quality<<" PASS\n";
  }
  cuDevicePrimaryCtxRelease(device);
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroyContext(display,context);eglDestroySurface(display,surface);eglTerminate(display);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
