#include "nvvfx_vsr/processor_config.h"
#include <iostream>
#include <cstdlib>
#include <stdexcept>
using namespace nvvfx_vsr;
void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
int main() {
  try {
    unsetenv("VFXSDK_ROOT");
    Check(ResolveSdkRoot("/opt/VideoFX")==std::optional<std::string>("/opt/VideoFX"),
          "unset SDK override uses compiled default");
    setenv("VFXSDK_ROOT","/tmp/SDK with spaces/VideoFX/",1);
    Check(ResolveSdkRoot("/opt/VideoFX")==std::optional<std::string>("/tmp/SDK with spaces/VideoFX"),
          "absolute SDK override is normalized and shared");
    for(const char* invalid:{"","relative/VideoFX","/","/tmp/../"}) {
      setenv("VFXSDK_ROOT",invalid,1);
      Check(!ResolveSdkRoot("/opt/VideoFX"),"invalid SDK override must fail closed");
    }
    unsetenv("VFXSDK_ROOT");

    setenv("NVVFX_VSR_TARGET_HEIGHT","1440",1);
    auto supersampled=SelectBrowserProcessorConfig({1920,1080});
    Check(supersampled && supersampled->quality==4 && supersampled->strength==1 &&
          supersampled->output.width==2560 && supersampled->output.height==1440,
          "1080p supersampling must request Ultra 1440p");
    auto low=SelectBrowserProcessorConfig({1280,720});
    Check(low && low->output.width==2560 && low->output.height==1440,
          "720p must also use the configured 1440p target");
    auto portrait=SelectBrowserProcessorConfig({360,640});
    Check(portrait && portrait->output.width==810 && portrait->output.height==1440,
          "1440p must preserve portrait aspect");
    Check(!SelectBrowserProcessorConfig({2560,1440}),"source above 1080p must retain bypass");
    for(const char* setting:{"1080","invalid","4320"}) {
      setenv("NVVFX_VSR_TARGET_HEIGHT",setting,1);
      auto safe=SelectBrowserProcessorConfig({1920,1080});
      Check(safe && safe->quality==11 && safe->output.width==1920 && safe->output.height==1080,
            "disabled or unknown target must retain native denoise");
    }
    setenv("NVVFX_VSR_TARGET_HEIGHT","1440",1);
    Check(!SelectBrowserProcessorConfig({1280,720},0),"Off must bypass even with enabled environment");
    for(int height:{1080,1440,2160}) {
      auto explicit_config=SelectBrowserProcessorConfig({1280,720},height);
      Check(explicit_config && explicit_config->output.height==height,
            "explicit browser setting must override environment");
    }
    for(int invalid:{-2,1,720,4320})
      Check(!SelectBrowserProcessorConfig({1280,720},invalid),"invalid explicit quality must bypass");
    Check(!SelectBrowserProcessorConfig({2560,1440},2160),"explicit target must preserve source cap");
    unsetenv("NVVFX_VSR_TARGET_HEIGHT");
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
