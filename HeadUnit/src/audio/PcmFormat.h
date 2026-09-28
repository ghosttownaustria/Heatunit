#pragma once
#include <cstddef>
#include <cstdint>

namespace headunit {
// The phone's audio streams (and the radio's own player, which plays as Media).
enum class AudioKind { Media = 0, Guidance = 1, System = 2 };
inline constexpr int kAudioKindCount = 3;

// The shape of interleaved PCM audio.
struct PcmFormat {
    std::uint32_t sampleRate{48000};
    std::uint32_t channels{2};
    std::uint32_t bitsPerSample{16};

    // Bytes of one sample for every channel.
    constexpr std::size_t BytesPerFrame() const { return static_cast<std::size_t>(channels) * bitsPerSample / 8; }
    // Bytes of one second of audio.
    constexpr std::size_t BytesPerSecond() const { return BytesPerFrame() * sampleRate; }
};

const char* AudioKindName(AudioKind kind);
}
