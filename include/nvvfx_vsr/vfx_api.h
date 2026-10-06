#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace nvvfx_vsr {

using StreamHandle = std::uintptr_t;
using ImageHandle = std::uintptr_t;
using EffectHandle = std::uintptr_t;

class IVfxApi {
 public:
  virtual ~IVfxApi() = default;

  virtual int SelectGpu(int gpu) = 0;
  virtual std::string DeviceName(int gpu) = 0;

  virtual int CreateStream(StreamHandle* stream) = 0;
  virtual void DestroyStream(StreamHandle stream) noexcept = 0;

  virtual int AllocateRgba8(int width, int height, ImageHandle* image) = 0;
  virtual void DeallocateImage(ImageHandle image) noexcept = 0;
  virtual int FillSynthetic(ImageHandle image, StreamHandle stream) = 0;

  virtual int CreateVideoSuperRes(EffectHandle* effect) = 0;
  virtual void DestroyEffect(EffectHandle effect) noexcept = 0;
  virtual int Configure(EffectHandle effect, ImageHandle input,
                        ImageHandle output, StreamHandle stream, int quality,
                        float strength) = 0;
  virtual int Load(EffectHandle effect) = 0;
  virtual int Run(EffectHandle effect, bool async) = 0;
  virtual int Synchronize(StreamHandle stream) = 0;

  virtual std::string StatusMessage(int status) = 0;
};

std::unique_ptr<IVfxApi> CreateNvidiaVfxApi();

}  // namespace nvvfx_vsr
