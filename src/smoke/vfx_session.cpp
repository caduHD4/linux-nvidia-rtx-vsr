#include "nvvfx_vsr/vfx_session.h"

#include <chrono>

namespace nvvfx_vsr {

VfxSession::~VfxSession() { Cleanup(); }

SmokeResult VfxSession::Run(const SmokeConfig& config) {
  Cleanup();

  int status = api_.SelectGpu(config.gpu);
  if (status != 0) return Failure(status, config, "select GPU");
  if ((status = api_.CreateStream(&stream_)) != 0) {
    return Failure(status, config, "create stream");
  }
  if ((status = api_.AllocateRgba8(config.input.width, config.input.height,
                                   &input_)) != 0) {
    return Failure(status, config, "allocate input");
  }
  if ((status = api_.AllocateRgba8(config.output.width, config.output.height,
                                   &output_)) != 0) {
    return Failure(status, config, "allocate output");
  }
  if ((status = api_.FillSynthetic(input_, stream_)) != 0) {
    return Failure(status, config, "fill input");
  }
  if ((status = api_.CreateVideoSuperRes(&effect_)) != 0) {
    return Failure(status, config, "create effect");
  }
  if ((status = api_.Configure(effect_, input_, output_, stream_,
                               config.quality, config.strength)) != 0) {
    return Failure(status, config, "configure effect");
  }
  if ((status = api_.Load(effect_)) != 0) {
    return Failure(status, config, "load effect");
  }

  for (int index = 0; index < config.warmup; ++index) {
    if ((status = api_.Run(effect_, true)) != 0) {
      return Failure(status, config, "warm up effect");
    }
  }
  if ((status = api_.Synchronize(stream_)) != 0) {
    return Failure(status, config, "synchronize warmup");
  }

  const auto start = std::chrono::steady_clock::now();
  for (int index = 0; index < config.iterations; ++index) {
    if ((status = api_.Run(effect_, true)) != 0) {
      return Failure(status, config, "run effect");
    }
  }
  if ((status = api_.Synchronize(stream_)) != 0) {
    return Failure(status, config, "synchronize benchmark");
  }
  const auto stop = std::chrono::steady_clock::now();

  const auto elapsed =
      std::chrono::duration<double, std::milli>(stop - start).count();
  return SmokeResult{
      .pass = true,
      .status = 0,
      .message = "PASS",
      .gpu_name = api_.DeviceName(config.gpu),
      .input = config.input,
      .output = config.output,
      .quality = config.quality,
      .average_ms = elapsed / config.iterations,
      .completed_iterations = config.iterations,
  };
}

SmokeResult VfxSession::Failure(int status, const SmokeConfig& config,
                                std::string_view stage) {
  return SmokeResult{
      .pass = false,
      .status = status,
      .message = std::string(stage) + ": " + api_.StatusMessage(status) +
                 "; verify matching NVIDIA VFX SDK Core and "
                 "nvvfxvideosuperres 1.3.0.0 are installed under VFXSDK_ROOT",
      .gpu_name = api_.DeviceName(config.gpu),
      .input = config.input,
      .output = config.output,
      .quality = config.quality,
  };
}

void VfxSession::Cleanup() noexcept {
  if (effect_ != 0) {
    api_.DestroyEffect(effect_);
    effect_ = 0;
  }
  if (output_ != 0) {
    api_.DeallocateImage(output_);
    output_ = 0;
  }
  if (input_ != 0) {
    api_.DeallocateImage(input_);
    input_ = 0;
  }
  if (stream_ != 0) {
    api_.DestroyStream(stream_);
    stream_ = 0;
  }
}

}  // namespace nvvfx_vsr
