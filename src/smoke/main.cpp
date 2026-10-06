#include "nvvfx_vsr/smoke_config.h"
#include "nvvfx_vsr/vfx_api.h"
#include "nvvfx_vsr/vfx_session.h"

#include <exception>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
  std::vector<std::string_view> arguments;
  arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
  for (int index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);

  try {
    const auto config = nvvfx_vsr::ParseSmokeConfig(arguments);
    if (config.help) {
      std::cout << nvvfx_vsr::SmokeUsage() << '\n';
      return 0;
    }
    auto api = nvvfx_vsr::CreateNvidiaVfxApi();
    nvvfx_vsr::VfxSession session(*api);
    const auto result = session.Run(config);
    if (!result.pass) {
      std::cerr << "VFX status " << result.status << ": " << result.message
                << '\n';
      return 1;
    }
    if (!std::isfinite(result.average_ms) || result.average_ms <= 0.0) {
      std::cerr << "vsr-smoke: measured timing is not finite and positive\n";
      return 1;
    }
    std::cout << "GPU: " << result.gpu_name << '\n'
              << "VSR quality: " << result.quality << '\n'
              << result.input.width << 'x' << result.input.height << " -> "
              << result.output.width << 'x' << result.output.height << '\n'
              << result.average_ms << " ms/frame\n"
              << "PASS\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "vsr-smoke: " << error.what() << '\n'
              << nvvfx_vsr::SmokeUsage() << '\n';
    return 2;
  }
}
