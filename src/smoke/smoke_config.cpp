#include "nvvfx_vsr/smoke_config.h"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <string>

namespace nvvfx_vsr {
namespace {

int ParseInteger(std::string_view value, std::string_view option) {
  int parsed = 0;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size()) {
    throw std::invalid_argument(std::string(option) + " requires an integer");
  }
  return parsed;
}

float ParseFloat(std::string_view value, std::string_view option) {
  float parsed = 0.0F;
  const auto result =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
      !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string(option) + " requires a number");
  }
  return parsed;
}

Dimensions ParseDimensions(std::string_view value, std::string_view option) {
  const auto separator = value.find_first_of("xX");
  if (separator == std::string_view::npos || separator == 0 ||
      separator + 1 == value.size() ||
      value.find_first_of("xX", separator + 1) != std::string_view::npos) {
    throw std::invalid_argument(std::string(option) + " requires WIDTHxHEIGHT");
  }
  const Dimensions dimensions{
      ParseInteger(value.substr(0, separator), option),
      ParseInteger(value.substr(separator + 1), option),
  };
  if (dimensions.width <= 0 || dimensions.height <= 0) {
    throw std::invalid_argument(std::string(option) + " must be positive");
  }
  return dimensions;
}

int ParseQuality(std::string_view value) {
  if (value == "low") return 1;
  if (value == "medium") return 2;
  if (value == "high") return 3;
  if (value == "ultra") return 4;
  const int quality = ParseInteger(value, "--quality");
  if (quality < 1 || quality > 4) {
    throw std::invalid_argument("--quality must be low, medium, high, ultra, or 1-4");
  }
  return quality;
}

std::string_view RequireValue(std::span<const std::string_view> arguments,
                              std::size_t* index) {
  if (*index + 1 >= arguments.size()) {
    throw std::invalid_argument(std::string(arguments[*index]) +
                                " requires a value");
  }
  return arguments[++(*index)];
}

}  // namespace

SmokeConfig ParseSmokeConfig(std::span<const std::string_view> arguments) {
  SmokeConfig config;
  for (std::size_t index = 0; index < arguments.size(); ++index) {
    const std::string_view option = arguments[index];
    if (option == "--help" || option == "-h") {
      config.help = true;
    } else if (option == "--gpu") {
      config.gpu = ParseInteger(RequireValue(arguments, &index), option);
    } else if (option == "--quality") {
      config.quality = ParseQuality(RequireValue(arguments, &index));
    } else if (option == "--strength") {
      config.strength = ParseFloat(RequireValue(arguments, &index), option);
    } else if (option == "--input") {
      config.input = ParseDimensions(RequireValue(arguments, &index), option);
    } else if (option == "--output") {
      config.output = ParseDimensions(RequireValue(arguments, &index), option);
    } else if (option == "--warmup") {
      config.warmup = ParseInteger(RequireValue(arguments, &index), option);
    } else if (option == "--iterations") {
      config.iterations = ParseInteger(RequireValue(arguments, &index), option);
    } else {
      throw std::invalid_argument("unknown option: " + std::string(option));
    }
  }

  if (config.gpu < 0) throw std::invalid_argument("--gpu must be non-negative");
  if (config.strength < 0.0F || config.strength > 1.0F) {
    throw std::invalid_argument("--strength must be between 0 and 1");
  }
  if (config.output.width <= config.input.width ||
      config.output.height <= config.input.height) {
    throw std::invalid_argument("--output must upscale both input dimensions");
  }
  if (config.warmup <= 0) {
    throw std::invalid_argument("--warmup must be greater than zero");
  }
  if (config.iterations <= 0) {
    throw std::invalid_argument("--iterations must be greater than zero");
  }
  return config;
}

std::string_view SmokeUsage() {
  return "Usage: vsr-smoke [--gpu N] [--quality low|medium|high|ultra|1-4] "
         "[--strength 0..1] [--input WIDTHxHEIGHT] [--output WIDTHxHEIGHT] "
         "[--warmup N] [--iterations N]";
}

}  // namespace nvvfx_vsr

