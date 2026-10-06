#include "nvvfx_vsr/vfx_session.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using nvvfx_vsr::EffectHandle;
using nvvfx_vsr::IVfxApi;
using nvvfx_vsr::ImageHandle;
using nvvfx_vsr::SmokeConfig;
using nvvfx_vsr::StreamHandle;
using nvvfx_vsr::VfxSession;

void Check(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

class FakeVfxApi final : public IVfxApi {
 public:
  explicit FakeVfxApi(int fail_at = 0) : fail_at_(fail_at) {}

  int SelectGpu(int) override { return Fallible("select_gpu"); }
  std::string DeviceName(int) override { return "Fake RTX"; }

  int CreateStream(StreamHandle* stream) override {
    const int status = Fallible("create_stream");
    if (status == 0) {
      *stream = 11;
      live_stream_ = true;
    }
    return status;
  }
  void DestroyStream(StreamHandle) noexcept override {
    events.emplace_back("destroy_stream");
    live_stream_ = false;
  }

  int AllocateRgba8(int, int, ImageHandle* image) override {
    const int status = Fallible("allocate_image");
    if (status == 0) {
      *image = next_image_++;
      ++live_images_;
    }
    return status;
  }
  void DeallocateImage(ImageHandle image) noexcept override {
    events.emplace_back("free_image_" + std::to_string(image));
    --live_images_;
  }
  int FillSynthetic(ImageHandle, StreamHandle) override {
    return Fallible("fill_synthetic");
  }

  int CreateVideoSuperRes(EffectHandle* effect) override {
    const int status = Fallible("create_effect");
    if (status == 0) {
      *effect = 31;
      live_effect_ = true;
    }
    return status;
  }
  void DestroyEffect(EffectHandle) noexcept override {
    events.emplace_back("destroy_effect");
    live_effect_ = false;
  }
  int Configure(EffectHandle, ImageHandle, ImageHandle, StreamHandle, int,
                float) override {
    return Fallible("configure");
  }
  int Load(EffectHandle) override {
    ++load_count;
    return Fallible("load");
  }
  int Run(EffectHandle, bool async) override {
    Check(async, "session requested synchronous VFX run");
    ++run_count;
    return Fallible("run");
  }
  int Synchronize(StreamHandle) override { return Fallible("synchronize"); }
  std::string StatusMessage(int status) override {
    return "fake status " + std::to_string(status);
  }

  bool HasLeaks() const {
    return live_stream_ || live_effect_ || live_images_ != 0;
  }

  std::vector<std::string> events;
  int load_count = 0;
  int run_count = 0;

 private:
  int Fallible(std::string event) {
    events.push_back(std::move(event));
    ++fallible_calls_;
    return fallible_calls_ == fail_at_ ? 700 : 0;
  }

  int fail_at_ = 0;
  int fallible_calls_ = 0;
  ImageHandle next_image_ = 21;
  int live_images_ = 0;
  bool live_stream_ = false;
  bool live_effect_ = false;
};

void TestSuccessfulLifecycle() {
  FakeVfxApi api;
  SmokeConfig config;
  {
    VfxSession session(api);
    const auto result = session.Run(config);
    Check(result.pass, "successful fake session did not pass");
    Check(result.gpu_name == "Fake RTX", "GPU name not propagated");
    Check(result.input.width == 1920 && result.output.width == 3840,
          "dimensions not propagated");
    Check(result.quality == 3, "quality not propagated");
    Check(result.completed_iterations == 15, "iteration count not propagated");
    Check(std::isfinite(result.average_ms) && result.average_ms > 0.0,
          "timing is not finite and positive");
  }

  const std::vector<std::string> expected_prefix = {
      "select_gpu",   "create_stream",  "allocate_image", "allocate_image",
      "fill_synthetic", "create_effect", "configure",      "load"};
  Check(api.events.size() >= expected_prefix.size(), "lifecycle too short");
  Check(std::equal(expected_prefix.begin(), expected_prefix.end(),
                   api.events.begin()),
        "initialization order changed");
  Check(api.load_count == 1, "effect loaded more than once");
  Check(api.run_count == config.warmup + config.iterations,
        "unexpected run count");
  const std::vector<std::string> expected_cleanup = {
      "destroy_effect", "free_image_22", "free_image_21", "destroy_stream"};
  Check(std::equal(expected_cleanup.rbegin(), expected_cleanup.rend(),
                   api.events.rbegin()),
        "resources not cleaned up in reverse acquisition order");
  Check(!api.HasLeaks(), "successful session leaked resources");
}

void TestEveryInitializationFailureCleansUp() {
  const std::string_view stages[] = {
      "select GPU", "create stream", "allocate input", "allocate output",
      "fill input", "create effect", "configure effect", "load effect"};
  for (int fail_at = 1; fail_at <= 8; ++fail_at) {
    FakeVfxApi api(fail_at);
    {
      VfxSession session(api);
      const auto result = session.Run(SmokeConfig{});
      Check(!result.pass, "injected failure reported PASS");
      Check(result.status == 700, "SDK status not propagated");
      Check(result.message.find(stages[fail_at - 1]) != std::string::npos,
            "failure stage not reported");
      Check(result.message.find("verify matching NVIDIA VFX SDK Core") !=
                std::string::npos,
            "runtime recovery hint not reported");
    }
    Check(!api.HasLeaks(), "failure path leaked a resource");
  }
}

void TestEveryExecutionFailureCleansUp() {
  struct FailureCase {
    int call;
    std::string_view stage;
  };
  const FailureCase cases[] = {
      {9, "warm up effect"},
      {12, "synchronize warmup"},
      {13, "run effect"},
      {28, "synchronize benchmark"},
  };
  for (const auto& failure : cases) {
    FakeVfxApi api(failure.call);
    {
      VfxSession session(api);
      const auto result = session.Run(SmokeConfig{});
      Check(!result.pass, "execution failure reported PASS");
      Check(result.message.find(failure.stage) != std::string::npos,
            "execution failure stage not reported");
    }
    Check(!api.HasLeaks(), "execution failure leaked a resource");
  }
}

}  // namespace

int main() {
  try {
    TestSuccessfulLifecycle();
    TestEveryInitializationFailureCleansUp();
    TestEveryExecutionFailureCleansUp();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
