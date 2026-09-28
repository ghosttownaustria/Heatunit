#pragma once
#include <atomic>
#include <cstdint>

namespace headunit {
// Who has the car's media sound: the phone (Android Auto) or the radio's own player. As in a car, the source that
// started last keeps it. When the radio's player starts, the window sends the phone its pause key; when the phone starts
// playing (after the radio's player did), the radio's player falls silent. This tracks the phone's side: when its media
// audio last arrived and when it last started after a pause.
class MediaActivity {
public:
    // Audio that follows a gap longer than this is a new start (the phone's stream stopped and began again); shorter
    // gaps are the usual jitter of a running stream.
    static constexpr std::int64_t kGapMs = 500;

    void NoteAudio(std::int64_t nowMs);
    std::int64_t StartMs() const;
    bool IsSounding(std::int64_t nowMs) const;

private:
    static constexpr std::int64_t kLongAgo = INT64_MIN / 4;

    std::atomic<std::int64_t> m_lastMs{kLongAgo};
    std::atomic<std::int64_t> m_startMs{kLongAgo};
};

// Whether the radio's own player, sounding since `localStartMs`, must fall silent because the phone plays and started
// after it. The phone's music that is still running out after it was sent its pause key does not count: it started
// before.
constexpr bool IsPhoneTakingOver(bool isLocalSounding, std::int64_t localStartMs, bool isPhoneSounding, std::int64_t phoneStartMs)
{
    return isLocalSounding && isPhoneSounding && phoneStartMs > localStartMs;
}
}
