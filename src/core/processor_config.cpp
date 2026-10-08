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
      !std::isfinite(c.strength) || c.strength < 0 || c.strength > 1 || !std::isfinite(c.sharpness) ||
      (c.sharpness != -1 && (c.sharpness < 0 || c.sharpness > 1))) return false;
  if (c.quality >= 8 && c.quality <= 11) return c.input.width == c.output.width &&
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

std::optional<ProcessorConfig> SelectBrowserProcessorConfig(Dimensions input, int target_height,
    int denoise_quality, float sharpness) {
  if(denoise_quality < 8 || denoise_quality > 11 || !std::isfinite(sharpness) ||
     (sharpness != -1 && (sharpness < 0 || sharpness > 1))) return std::nullopt;
  auto select = [=](Dimensions target) -> std::optional<ProcessorConfig> {
    auto config = SelectProcessorConfig(input,target);
    if(!config) return std::nullopt;
    if(config->quality == 11) config->quality = denoise_quality;
    config->sharpness = sharpness;
    return config;
  };
  // Higher-resolution inputs denoise in place, independently of upscale target.
  // Validate Off/invalid settings first so they cannot enable native processing.
  if(target_height != -1 && target_height != 1080 && target_height != 1440 &&
     target_height != 2160) return std::nullopt;
  if(input.width>4096 || input.height>2160) return std::nullopt;
  if(input.width>1920 || input.height>1080) {
    auto config = select(input);
    if(config) config->quality = 8;  // Bound the cost of native 1440p/4K denoise.
    return config;
  }
  if(target_height != -1) {
    if(target_height != 1080 && target_height != 1440 && target_height != 2160)
      return std::nullopt;
    const Dimensions target=target_height == 2160 ? Dimensions{3840,2160} :
        target_height == 1440 ? Dimensions{2560,1440} : Dimensions{1920,1080};
    return select(target);
  }
  const char* value=std::getenv("NVVFX_VSR_TARGET_HEIGHT");
  const Dimensions target=value && std::strcmp(value,"2160")==0 ?
      Dimensions{3840,2160} :
      value && std::strcmp(value,"1440")==0 ?
      Dimensions{2560,1440} : Dimensions{1920,1080};
  return select(target);
}

}
