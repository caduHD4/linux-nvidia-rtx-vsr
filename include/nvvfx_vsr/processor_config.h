#pragma once
#include "nvvfx_vsr/dimensions.h"
#include <optional>
namespace nvvfx_vsr {
struct ProcessorConfig {
  Dimensions input;
  Dimensions output;
  int quality;
  float strength = 1.0F;
};
std::optional<ProcessorConfig> SelectProcessorConfig(Dimensions input,
                                                    Dimensions target);
bool ValidProcessorConfig(const ProcessorConfig& config);
}
