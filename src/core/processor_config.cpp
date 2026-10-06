#include "nvvfx_vsr/processor_config.h"
#include <algorithm>
#include <cmath>
namespace nvvfx_vsr {
namespace {
bool ValidSize(Dimensions d) { return d.width > 0 && d.height > 0 &&
  d.width <= 8192 && d.height <= 8192; }
}
bool ValidProcessorConfig(const ProcessorConfig& c) {
  if (!ValidSize(c.input) || !ValidSize(c.output) ||
      !std::isfinite(c.strength) || c.strength < 0 || c.strength > 1) return false;
  if (c.quality == 11) return c.input.width == c.output.width &&
                             c.input.height == c.output.height;
  return c.quality == 4 && c.output.width > c.input.width &&
                          c.output.height > c.input.height;
}
std::optional<ProcessorConfig> SelectProcessorConfig(Dimensions in, Dimensions target) {
  if (!ValidSize(in) || !ValidSize(target) ||
      in.width > target.width || in.height > target.height) return std::nullopt;
  const double scale = std::min(double(target.width)/in.width,
                                double(target.height)/in.height);
  if (scale <= 1.0) return ProcessorConfig{in,in,11,1.0F};
  Dimensions out{std::min(target.width, int(std::lround(in.width*scale/2))*2),
                 std::min(target.height, int(std::lround(in.height*scale/2))*2)};
  ProcessorConfig result{in,out,4,1.0F};
  return ValidProcessorConfig(result) ? std::optional(result) : std::nullopt;
}
}
