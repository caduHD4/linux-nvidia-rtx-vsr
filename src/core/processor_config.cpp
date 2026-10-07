#include "nvvfx_vsr/processor_config.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
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
std::optional<std::string> ResolveSdkRoot(const std::string& compiled_default) {
  const char* override_root = std::getenv("VFXSDK_ROOT");
  const std::filesystem::path path(override_root ? override_root : compiled_default);
  if (!path.is_absolute()) return std::nullopt;
  std::string normalized = path.lexically_normal().string();
  while (normalized.size() > 1 && normalized.back() == '/') normalized.pop_back();
  if (normalized == "/") return std::nullopt;
  return normalized;
}

std::optional<ProcessorConfig> SelectBrowserProcessorConfig(Dimensions input) {
  // Preserve the source cap and fail closed to the previous output preset.
  if(input.width>1920 || input.height>1080) return std::nullopt;
  const char* value=std::getenv("NVVFX_VSR_TARGET_HEIGHT");
  const Dimensions target=value && std::strcmp(value,"2160")==0 ?
      Dimensions{3840,2160} :
      value && std::strcmp(value,"1440")==0 ?
      Dimensions{2560,1440} : Dimensions{1920,1080};
  return SelectProcessorConfig(input,target);
}

}
