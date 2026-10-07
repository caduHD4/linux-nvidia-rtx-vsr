#pragma once
#include "nvvfx_vsr/processor_config.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
struct CUctx_st;
namespace nvvfx_vsr {
// Device buffers stay alive until Poll reports Ready or Drain returns success.
struct CudaFrameView {
  std::uint64_t device_pointer = 0;
  std::size_t pitch = 0;
  Dimensions size{};
};
enum class ProcessState { Ready, Pending, Failed };
class VsrProcessor {
 public:
  // Call once before entering the GPU sandbox. Loads/version-checks only;
  // retains module handles for process lifetime, without creating an effect.
  static bool PreloadRuntime(const std::string& sdk_root,std::string* error);
  static std::unique_ptr<VsrProcessor> Create(const ProcessorConfig& config,
      const std::string& sdk_root, CUctx_st* context, std::string* error);
  ~VsrProcessor();
  VsrProcessor(const VsrProcessor&) = delete;
  VsrProcessor& operator=(const VsrProcessor&) = delete;
  bool Submit(CudaFrameView input, CudaFrameView output, std::string* error);
  ProcessState Poll(std::string* error);
  bool Drain(std::string* error);
  // False requires retaining the owning CUDA context until process exit.
  bool Close(std::string* error);
  float LastGpuMilliseconds() const;
 private:
  struct Impl;
  explicit VsrProcessor(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
}
