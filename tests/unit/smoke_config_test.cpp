#include "nvvfx_vsr/smoke_config.h"

#include <cmath>
#include <exception>
#include <iostream>
#include <initializer_list>
#include <stdexcept>
#include <string_view>

namespace {

using nvvfx_vsr::ParseSmokeConfig;

nvvfx_vsr::SmokeConfig Parse(
    std::initializer_list<std::string_view> arguments) {
  return ParseSmokeConfig(
      std::span<const std::string_view>(arguments.begin(), arguments.size()));
}

void Check(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

template <typename Callable>
void CheckRejected(Callable&& callable, std::string_view name) {
  try {
    callable();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(std::string(name) + " was accepted");
}

void TestDefaults() {
  const auto config = Parse({});
  Check(config.gpu == 0, "default GPU");
  Check(config.quality == 3, "default quality");
  Check(std::abs(config.strength - 1.0F) < 0.0001F, "default strength");
  Check(config.input.width == 1920 && config.input.height == 1080,
        "default input");
  Check(config.output.width == 3840 && config.output.height == 2160,
        "default output");
  Check(config.warmup == 3, "default warm-up");
  Check(config.iterations == 15, "default iterations");
}

void TestQualityNamesAndNumbers() {
  constexpr std::string_view names[] = {"low", "medium", "high", "ultra"};
  constexpr std::string_view numbers[] = {"1", "2", "3", "4"};
  for (int index = 0; index < 4; ++index) {
    Check(Parse({"--quality", names[index]}).quality == index + 1,
          "named quality");
    Check(Parse({"--quality", numbers[index]}).quality == index + 1,
          "numeric quality");
  }
}

void TestExplicitValues() {
  const auto config = Parse({
      "--gpu", "1", "--strength", "0.5", "--input", "1280x720",
      "--output", "2560X1440", "--warmup", "4", "--iterations", "20"});
  Check(config.gpu == 1, "explicit GPU");
  Check(std::abs(config.strength - 0.5F) < 0.0001F, "explicit strength");
  Check(config.input.width == 1280 && config.input.height == 720,
        "explicit input");
  Check(config.output.width == 2560 && config.output.height == 1440,
        "explicit output");
  Check(config.warmup == 4, "explicit warm-up");
  Check(config.iterations == 20, "explicit iterations");
}

void TestRejectedInputs() {
  CheckRejected([] { Parse({"--input", "1920"}); },
                "malformed dimensions");
  CheckRejected([] { Parse({"--input", "0x1080"}); },
                "zero dimension");
  CheckRejected([] { Parse({"--output", "1280x720"}); },
                "non-upscaling output");
  CheckRejected([] { Parse({"--strength", "-0.1"}); },
                "negative strength");
  CheckRejected([] { Parse({"--strength", "1.1"}); },
                "strength above one");
  CheckRejected([] { Parse({"--quality", "0"}); },
                "bicubic quality");
  CheckRejected([] { Parse({"--quality", "5"}); },
                "reserved quality");
  CheckRejected([] { Parse({"--warmup", "0"}); },
                "zero warm-up");
  CheckRejected([] { Parse({"--iterations", "0"}); },
                "zero iterations");
  CheckRejected([] { Parse({"--unknown", "value"}); },
                "unknown option");
  CheckRejected([] { Parse({"--gpu"}); }, "missing option value");
}

}  // namespace

int main() {
  try {
    TestDefaults();
    TestQualityNamesAndNumbers();
    TestExplicitValues();
    TestRejectedInputs();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
