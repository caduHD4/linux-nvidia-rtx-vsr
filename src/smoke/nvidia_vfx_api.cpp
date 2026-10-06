#include "nvvfx_vsr/vfx_api.h"

#include <nvCVImage.h>
#include <nvVFXVideoSuperRes.h>
#include <nvVideoEffects.h>

#include <array>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace nvvfx_vsr {
namespace {

template <typename Pointer>
std::uintptr_t ToHandle(Pointer pointer) {
  return reinterpret_cast<std::uintptr_t>(pointer);
}

template <typename Pointer>
Pointer FromHandle(std::uintptr_t handle) {
  return reinterpret_cast<Pointer>(handle);
}

struct PipeCloser {
  void operator()(FILE* pipe) const noexcept {
    if (pipe != nullptr) (void)pclose(pipe);
  }
};

std::string QueryGpuName(int gpu) {
  std::unique_ptr<FILE, PipeCloser> pipe(
      popen("nvidia-smi --query-gpu=name --format=csv,noheader,nounits 2>/dev/null",
            "r"));
  if (!pipe) return "GPU " + std::to_string(gpu);

  std::array<char, 256> buffer{};
  for (int index = 0; fgets(buffer.data(), buffer.size(), pipe.get()); ++index) {
    if (index != gpu) continue;
    std::string name(buffer.data());
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r')) {
      name.pop_back();
    }
    return name.empty() ? "GPU " + std::to_string(gpu) : name;
  }
  return "GPU " + std::to_string(gpu);
}

class NvidiaVfxApi final : public IVfxApi {
 public:
  int SelectGpu(int gpu) override {
    unsigned version = 0;
    const NvCV_Status version_status = NvVFX_GetVersion(&version);
    if (version_status != NVCV_SUCCESS) {
      return static_cast<int>(version_status);
    }
    constexpr unsigned required_version = (1U << 24U) | (3U << 16U);
    if (version != required_version) {
      return static_cast<int>(NVCV_ERR_VERSION_MISMATCH);
    }
    return static_cast<int>(NvVFX_SetS32(nullptr, NVVFX_GPU, gpu));
  }

  std::string DeviceName(int gpu) override { return QueryGpuName(gpu); }

  int CreateStream(StreamHandle* stream) override {
    CUstream cuda_stream = nullptr;
    const NvCV_Status status = NvVFX_CudaStreamCreate(&cuda_stream);
    if (status == NVCV_SUCCESS) *stream = ToHandle(cuda_stream);
    return static_cast<int>(status);
  }

  void DestroyStream(StreamHandle stream) noexcept override {
    (void)NvVFX_CudaStreamDestroy(FromHandle<CUstream>(stream));
  }

  int AllocateRgba8(int width, int height, ImageHandle* image) override {
    auto allocation = std::make_unique<NvCVImage>();
    const NvCV_Status status = NvCVImage_Alloc(
        allocation.get(), static_cast<unsigned>(width),
        static_cast<unsigned>(height), NVCV_RGBA, NVCV_U8, NVCV_INTERLEAVED,
        NVCV_GPU, 32);
    if (status == NVCV_SUCCESS) *image = ToHandle(allocation.release());
    return static_cast<int>(status);
  }

  void DeallocateImage(ImageHandle image) noexcept override {
    std::unique_ptr<NvCVImage> allocation(FromHandle<NvCVImage*>(image));
    NvCVImage_Dealloc(allocation.get());
  }

  int FillSynthetic(ImageHandle image, StreamHandle stream) override {
    NvCVImage* destination = FromHandle<NvCVImage*>(image);
    const std::size_t pitch = static_cast<std::size_t>(destination->width) * 4;
    std::vector<unsigned char> pixels(
        pitch * static_cast<std::size_t>(destination->height));
    for (unsigned y = 0; y < destination->height; ++y) {
      for (unsigned x = 0; x < destination->width; ++x) {
        const std::size_t offset = static_cast<std::size_t>(y) * pitch + x * 4;
        pixels[offset + 0] = static_cast<unsigned char>((x * 255U) /
                                                        destination->width);
        pixels[offset + 1] = static_cast<unsigned char>((y * 255U) /
                                                        destination->height);
        pixels[offset + 2] = static_cast<unsigned char>((x ^ y) & 0xffU);
        pixels[offset + 3] = 255;
      }
    }

    NvCVImage source{};
    NvCV_Status status = NvCVImage_Init(
        &source, destination->width, destination->height,
        static_cast<int>(pitch), pixels.data(), NVCV_RGBA, NVCV_U8,
        NVCV_INTERLEAVED, NVCV_CPU);
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    status = NvCVImage_Transfer(&source, destination, 1.0F,
                               FromHandle<CUstream>(stream), nullptr);
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    return static_cast<int>(
        NvVFX_CudaStreamSynchronize(FromHandle<CUstream>(stream)));
  }

  int CreateVideoSuperRes(EffectHandle* effect) override {
    NvVFX_Handle vfx_effect = nullptr;
    const NvCV_Status status =
        NvVFX_CreateEffect(NVVFX_FX_VIDEO_SUPER_RES, &vfx_effect);
    if (status == NVCV_SUCCESS) *effect = ToHandle(vfx_effect);
    return static_cast<int>(status);
  }

  void DestroyEffect(EffectHandle effect) noexcept override {
    NvVFX_DestroyEffect(FromHandle<NvVFX_Handle>(effect));
  }

  int Configure(EffectHandle effect, ImageHandle input, ImageHandle output,
                StreamHandle stream, int quality, float strength) override {
    NvVFX_Handle vfx_effect = FromHandle<NvVFX_Handle>(effect);
    NvCV_Status status = NvVFX_SetImage(
        vfx_effect, NVVFX_INPUT_IMAGE, FromHandle<NvCVImage*>(input));
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    status = NvVFX_SetImage(vfx_effect, NVVFX_OUTPUT_IMAGE,
                            FromHandle<NvCVImage*>(output));
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    status = NvVFX_SetCudaStream(vfx_effect, NVVFX_CUDA_STREAM,
                                 FromHandle<CUstream>(stream));
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    status = NvVFX_SetU32(vfx_effect, NVVFX_QUALITY_LEVEL,
                          static_cast<unsigned>(quality));
    if (status != NVCV_SUCCESS) return static_cast<int>(status);
    return static_cast<int>(NvVFX_SetF32(vfx_effect, NVVFX_STRENGTH, strength));
  }

  int Load(EffectHandle effect) override {
    return static_cast<int>(NvVFX_Load(FromHandle<NvVFX_Handle>(effect)));
  }

  int Run(EffectHandle effect, bool async) override {
    return static_cast<int>(
        NvVFX_Run(FromHandle<NvVFX_Handle>(effect), async ? 1 : 0));
  }

  int Synchronize(StreamHandle stream) override {
    return static_cast<int>(
        NvVFX_CudaStreamSynchronize(FromHandle<CUstream>(stream)));
  }

  std::string StatusMessage(int status) override {
    const char* message =
        NvCV_GetErrorStringFromCode(static_cast<NvCV_Status>(status));
    return message == nullptr ? "NVIDIA VFX status " + std::to_string(status)
                              : std::string(message);
  }
};

}  // namespace

std::unique_ptr<IVfxApi> CreateNvidiaVfxApi() {
  return std::make_unique<NvidiaVfxApi>();
}

}  // namespace nvvfx_vsr
