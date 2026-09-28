#include "audio/AudioState.h"
#include "audio/AudioClock.h"
#include <algorithm>

namespace headunit {
namespace {
// How long after its last audio a stream still counts as active on the display.
constexpr std::int64_t kActiveMs = 500;
}

// The volume step, 0 to kMaxVolume.
int AudioState::Volume() const
{
    return m_volume;
}

// Whether the sound is muted.
bool AudioState::IsMuted() const
{
    return m_isMuted;
}

// Sets the volume step, limited to 0 to kMaxVolume.
void AudioState::SetVolume(int volume)
{
    m_volume = std::clamp(volume, 0, kMaxVolume);
}

// Turns the volume by `delta` steps. Turning it up while muted unmutes, like a car radio.
void AudioState::ChangeVolume(int delta)
{
    if (delta > 0) m_isMuted = false;
    SetVolume(m_volume + delta);
}

// Mutes or unmutes.
void AudioState::SetMuted(bool isMuted)
{
    m_isMuted = isMuted;
}

// Mutes when unmuted and the other way round.
void AudioState::ToggleMute()
{
    m_isMuted = !m_isMuted;
}

// Linear gain applied to the samples; squared so that the steps sound even (step 30 = full scale).
float AudioState::Gain() const
{
    if (m_isMuted) return 0.0f;
    const float fraction = static_cast<float>(m_volume) / kMaxVolume;
    return fraction * fraction;
}

// Output side: a block of `bytes` with this peak arrived for the stream.
void AudioState::ReportAudio(AudioKind kind, float peak, std::size_t bytes)
{
    auto& meter = m_meters[static_cast<int>(kind)];
    float current = meter.peak.load();
    while (peak > current && !meter.peak.compare_exchange_weak(current, peak)) {}
    meter.bytes += bytes;
    meter.lastAudioMs = SteadyNowMs();
}

// Display side: the peak since the last read (and resets it), and whether audio arrived recently.
AudioState::Meter AudioState::ReadMeter(AudioKind kind)
{
    auto& meter = m_meters[static_cast<int>(kind)];
    return {meter.peak.exchange(0.0f), SteadyNowMs() - meter.lastAudioMs.load() < kActiveMs};
}

// Bytes the phone sent for the stream.
std::uint64_t AudioState::BytesPlayed(AudioKind kind) const
{
    return m_meters[static_cast<int>(kind)].bytes;
}

// Output side: `bytes` of the stream were handed to the sound device.
void AudioState::ReportRendered(AudioKind kind, std::size_t bytes)
{
    m_meters[static_cast<int>(kind)].rendered += bytes;
}

// Bytes of the stream actually handed to the sound device.
std::uint64_t AudioState::BytesRendered(AudioKind kind) const
{
    return m_meters[static_cast<int>(kind)].rendered;
}
}
