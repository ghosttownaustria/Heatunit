#pragma once
#include <cstdint>
#include <span>

namespace headunit {
float PeakOfPcm16(std::span<const std::uint8_t> bytes);
void ApplyGainPcm16(std::span<std::uint8_t> bytes, float gain);
}
