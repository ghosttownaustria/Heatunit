#pragma once
#include "audio/AudioTypes.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

namespace headunit {
inline std::int64_t SteadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Who has the car's media sound: the phone (Android Auto) or the radio's own player. As in a car, the source that
// started last keeps it. When the radio's player starts, the window sends the phone its pause key; when the phone
// starts playing (after the radio's player did), the radio's player falls silent. This tracks the phone's side: when its
// media audio last arrived and when it last started after a pause.
class MediaActivity {
public:
    // Audio that follows a gap longer than this is a new start (the phone's stream stopped and began again); shorter
    // gaps are the usual jitter of a running stream.
    static constexpr std::int64_t kGapMs = 500;
    // A block of the phone's media audio arrived (protocol thread).
    void NoteAudio(std::int64_t nowMs) {
        if (nowMs - m_lastMs.load() > kGapMs) m_startMs = nowMs;
        m_lastMs = nowMs;
    }
    std::int64_t StartMs() const { return m_startMs; }
    bool IsSounding(std::int64_t nowMs) const { return nowMs - m_lastMs.load() <= kGapMs; }
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

// The phone's media output, telling `activity` about every block the phone plays.
class WatchedOutput final : public IPcmOutput {
public:
    WatchedOutput(std::shared_ptr<IPcmOutput> output, std::shared_ptr<MediaActivity> activity)
        : m_output(std::move(output)), m_activity(std::move(activity)) {}
    void Write(std::span<const std::uint8_t> pcm) override {
        if (!pcm.empty()) m_activity->NoteAudio(SteadyNowMs());
        m_output->Write(pcm);
    }
    void Flush() override { m_output->Flush(); }
    std::size_t Queued() const override { return m_output->Queued(); }
private:
    std::shared_ptr<IPcmOutput> m_output;
    std::shared_ptr<MediaActivity> m_activity;
};
}
