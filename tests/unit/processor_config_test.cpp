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
    auto higher=SelectBrowserProcessorConfig({2560,1440});
    Check(higher && higher->quality==11 && higher->output.width==2560 && higher->output.height==1440,
          "1440p source must denoise at native resolution");
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
    for(int target:{1080,1440,2160}) {
      for(Dimensions input:{Dimensions{2560,1440},Dimensions{3840,2160},Dimensions{4096,2160}}) {
        auto native_high=SelectBrowserProcessorConfig(input,target,8,0.65F);
        Check(native_high && native_high->quality==8 && native_high->output.width==input.width &&
              native_high->output.height==input.height && native_high->sharpness==0.65F,
              "high-resolution source must keep native dimensions and image settings");
      }
    }
    Check(!SelectBrowserProcessorConfig({3840,2160},0),"Off must bypass high-resolution denoise");
    Check(!SelectBrowserProcessorConfig({3840,2160},720),"invalid target must bypass high-resolution denoise");
    Check(!SelectBrowserProcessorConfig({7680,4320},2160),"8K source must retain bounded bypass");
    Check(!SelectBrowserProcessorConfig({4097,2160},2160),"over-width source must bypass");
    unsetenv("NVVFX_VSR_TARGET_HEIGHT");
    for(int mode:{8,9,10,11}) {
      auto denoise=SelectBrowserProcessorConfig({1920,1080},1080,mode,0.65F);
      Check(denoise && denoise->quality==mode && denoise->sharpness==0.65F,
            "native denoise mode and sharpness must follow browser settings");
      auto scale=SelectBrowserProcessorConfig({1280,720},2160,mode,0.0F);
      Check(scale && scale->quality==4 && scale->strength==1.0F && scale->sharpness==0,
            "upscaling must remain VSR Ultra and allow sharpening off");
    }
    Check(!SelectBrowserProcessorConfig({1920,1080},1080,7,0.35F),"reserved denoise mode must bypass");
    for(float invalid:{-2.0F,1.01F})
      Check(!SelectBrowserProcessorConfig({1280,720},2160,11,invalid),"invalid sharpness must bypass");
    Check(!ValidProcessorConfig({{1280,720},{1920,1080},8,1.0F}),"denoise Low cannot upscale");
    Check(!ValidProcessorConfig({{1280,720},{1920,1080},4,1.0F,2.0F}),"out-of-range sharpening must fail");
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
