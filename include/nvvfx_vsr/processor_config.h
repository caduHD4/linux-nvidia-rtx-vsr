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
};
std::optional<ProcessorConfig> SelectProcessorConfig(Dimensions input,
                                                    Dimensions target);
// Browser-only output preset; source eligibility remains limited to 1080p.
std::optional<ProcessorConfig> SelectBrowserProcessorConfig(Dimensions input);
// Shared by pre-sandbox broker/preload and GPU worker. Invalid overrides fail closed.
std::optional<std::string> ResolveSdkRoot(const std::string& compiled_default);
bool ValidProcessorConfig(const ProcessorConfig& config);
}
