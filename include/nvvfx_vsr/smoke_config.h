#pragma once

#include "nvvfx_vsr/dimensions.h"
#include <span>
#include <string_view>

namespace nvvfx_vsr {

struct SmokeConfig {
  int gpu = 0;
  int quality = 3;
  float strength = 1.0F;
  Dimensions input{1920, 1080};
  Dimensions output{3840, 2160};
  int warmup = 3;
  int iterations = 15;
  bool help = false;
};

SmokeConfig ParseSmokeConfig(std::span<const std::string_view> arguments);
std::string_view SmokeUsage();

}  // namespace nvvfx_vsr

