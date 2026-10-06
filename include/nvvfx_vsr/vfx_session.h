#pragma once

#include "nvvfx_vsr/smoke_config.h"
#include "nvvfx_vsr/vfx_api.h"

#include <string>
#include <string_view>

namespace nvvfx_vsr {

struct SmokeResult {
  bool pass = false;
  int status = 0;
  std::string message;
  std::string gpu_name;
  Dimensions input{};
  Dimensions output{};
  int quality = 0;
  double average_ms = 0.0;
  int completed_iterations = 0;
};

class VfxSession {
 public:
  explicit VfxSession(IVfxApi& api) : api_(api) {}
  ~VfxSession();

  VfxSession(const VfxSession&) = delete;
  VfxSession& operator=(const VfxSession&) = delete;

  SmokeResult Run(const SmokeConfig& config);

 private:
  SmokeResult Failure(int status, const SmokeConfig& config,
                      std::string_view stage);
  void Cleanup() noexcept;

  IVfxApi& api_;
  StreamHandle stream_ = 0;
  ImageHandle input_ = 0;
  ImageHandle output_ = 0;
  EffectHandle effect_ = 0;
};

}  // namespace nvvfx_vsr
