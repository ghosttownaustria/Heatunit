#pragma once
#include "audio/PcmFormat.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace headunit {
// The simulated car audio system: master volume in radio-style steps, mute, and per-stream activity for the display.
// Shared by the output threads and the window.
class AudioState {
public:
    static constexpr int kMaxVolume = 30;

    // What the display shows for one stream: the peak since its last look, and whether audio arrived recently.
    struct Meter {
        float peak{};
        bool isActive{};
    };

    int Volume() const;
    bool IsMuted() const;
    void SetVolume(int volume);
    void ChangeVolume(int delta);
    void SetMuted(bool isMuted);
    void ToggleMute();
    float Gain() const;
    void ReportAudio(AudioKind kind, float peak, std::size_t bytes);
    Meter ReadMeter(AudioKind kind);
    std::uint64_t BytesPlayed(AudioKind kind) const;
    void ReportRendered(AudioKind kind, std::size_t bytes);
    std::uint64_t BytesRendered(AudioKind kind) const;

private:
    // The counters of one stream.
    struct StreamMeter {
        std::atomic<float> peak{0.0f};
        std::atomic<std::uint64_t> bytes{0};
        std::atomic<std::uint64_t> rendered{0};
        std::atomic<std::int64_t> lastAudioMs{-1000000};
    };

    std::atomic<int> m_volume{15};
    std::atomic_bool m_isMuted{false};
    std::array<StreamMeter, kAudioKindCount> m_meters;
};
}
