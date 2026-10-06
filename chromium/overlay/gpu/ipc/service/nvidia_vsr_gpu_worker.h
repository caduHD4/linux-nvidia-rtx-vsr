#ifndef GPU_IPC_SERVICE_NVIDIA_VSR_GPU_WORKER_H_
#define GPU_IPC_SERVICE_NVIDIA_VSR_GPU_WORKER_H_
#include <cstddef>
#include <memory>
#include <string>
#include "base/functional/callback.h"
#include "base/memory/scoped_refptr.h"
#include "nvvfx_vsr/processor_config.h"
#include "ui/gl/gl_bindings.h"
namespace gl { class GLContext; class GLSurface; }
namespace gpu {
struct NvidiaVsrWorkerResult {
  bool success=false;
  bool quarantine=false;
  bool source_ready=false;
  float gpu_ms=0;
  std::string status;
};
// Receives only private, shared native GL textures. Never receives a
// SharedImage representation, VideoFrame, mailbox, or owning-sequence access.
class NvidiaVsrGpuWorker {
 public:
  NvidiaVsrGpuWorker(scoped_refptr<gl::GLContext> context,
                     scoped_refptr<gl::GLSurface> surface);
  ~NvidiaVsrGpuWorker();
  void ResetSlot(std::size_t slot,base::OnceCallback<void(bool)> callback);
  void Run(std::size_t slot,nvvfx_vsr::ProcessorConfig config,
           GLuint input,GLuint output,GLsync source_fence,
           base::OnceClosure source_ready,
           base::OnceCallback<bool()> may_start,
           base::OnceCallback<void(NvidiaVsrWorkerResult)> done);
  void Shutdown(base::OnceCallback<void(bool)> callback);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace gpu
#endif
