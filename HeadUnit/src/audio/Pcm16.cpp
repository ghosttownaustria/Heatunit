#include "audio/Pcm16.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace headunit {
// Peak of interleaved 16-bit PCM, 0..1. `bytes` may hold a trailing partial sample.
float PeakOfPcm16(std::span<const std::uint8_t> bytes)
{
    int peak = 0;
    for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
        std::int16_t sample;
        std::memcpy(&sample, bytes.data() + index, sizeof(sample));
        peak = std::max(peak, sample == INT16_MIN ? 32768 : std::abs(static_cast<int>(sample)));
    }
    return static_cast<float>(peak) / 32768.0f;
}

// Scales interleaved 16-bit PCM in place. Gain 1 leaves it untouched, 0 silences it.
void ApplyGainPcm16(std::span<std::uint8_t> bytes, float gain)
{
    if (gain >= 1.0f) return;
    if (gain <= 0.0f) {
        std::fill(bytes.begin(), bytes.end(), std::uint8_t{0});
        return;
    }
    for (std::size_t index = 0; index + 1 < bytes.size(); index += 2) {
        std::int16_t sample;
        std::memcpy(&sample, bytes.data() + index, sizeof(sample));
        sample = static_cast<std::int16_t>(std::clamp(static_cast<int>(static_cast<float>(sample) * gain), -32768, 32767));
        std::memcpy(bytes.data() + index, &sample, sizeof(sample));
    }
}
}
