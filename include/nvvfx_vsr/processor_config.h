#pragma once
#include "nvvfx_vsr/dimensions.h"
#include <optional>
#include <string>
namespace nvvfx_vsr {
struct ProcessorConfig {
  Dimensions input;
  Dimensions output;
  int quality;
  float strength = 1.0F;
  float sharpness = -1.0F; // -1 preserves the legacy environment preset.
};
std::optional<ProcessorConfig> SelectProcessorConfig(Dimensions input,
                                                    Dimensions target);
// Browser preset: upscale <=1080p, native denoise above it through 4096x2160.
std::optional<ProcessorConfig> SelectBrowserProcessorConfig(Dimensions input,
                                                            int target_height = -1,
                                                            int denoise_quality = 11,
                                                            float sharpness = -1.0F);
// Shared by pre-sandbox broker/preload and GPU worker. Invalid overrides fail closed.
std::optional<std::string> ResolveSdkRoot(const std::string& compiled_default);
bool ValidProcessorConfig(const ProcessorConfig& config);
}
