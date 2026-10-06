#include "nvvfx_vsr/processor_config.h"
#include <iostream>
#include <stdexcept>
using namespace nvvfx_vsr;
void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    auto up = SelectProcessorConfig({1280,720}, {1920,1080});
    Check(up && up->quality == 4 && up->strength == 1.0F &&
          up->output.width == 1920 && up->output.height == 1080,
          "720p must upscale to 1080p at maximum quality");
    auto native = SelectProcessorConfig({1920,1080}, {1920,1080});
    Check(native && native->quality == 11 && native->output.width == 1920 &&
          native->output.height == 1080, "native video must denoise without resize");
    Check(!SelectProcessorConfig({3840,2160}, {1920,1080}), "4K must bypass");
    auto vertical = SelectProcessorConfig({360,640}, {1920,1080});
    Check(vertical && vertical->output.width == 608 && vertical->output.height == 1080,
          "vertical video must fit height and keep aspect within rounding error");
    Check(!SelectProcessorConfig({0,720}, {1920,1080}), "zero width must bypass");
    Check(!SelectProcessorConfig({1280,720}, {-1,1080}), "invalid target must bypass");
    Check(!ValidProcessorConfig({{1280,720},{1920,1080},11,1.0F}),
          "denoise cannot upscale");
    Check(!ValidProcessorConfig({{1920,1080},{1280,720},4,1.0F}),
          "VSR cannot downscale");
    Check(!ValidProcessorConfig({{1280,720},{1920,1080},5,1.0F}),
          "reserved quality must fail");
    Check(!ValidProcessorConfig({{1280,720},{1920,1080},4,2.0F}),
          "invalid strength must fail");
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
