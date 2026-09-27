#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace headunit {
enum class AudioKind { Media = 0, Guidance = 1, System = 2 };
constexpr int kAudioKindCount = 3;
struct PcmFormat {
    std::uint32_t sampleRate{48000};
    std::uint32_t channels{2};
    std::uint32_t bitsPerSample{16};
    std::size_t BytesPerFrame() const { return static_cast<std::size_t>(channels) * bitsPerSample / 8; }
    std::size_t BytesPerSecond() const { return BytesPerFrame() * sampleRate; }
};

// Where the phone's audio goes. Write runs on the protocol thread and must not block.
class IPcmOutput {
public:
    virtual ~IPcmOutput() = default;
    virtual void Write(std::span<const std::uint8_t> pcm) = 0;
    // Drops what is queued (the stream stopped).
    virtual void Flush() = 0;
    // Bytes written but not yet handed to the sound device. A source faster than real time (a file decoder)
    // waits while this is high instead of overrunning the queue.
    virtual std::size_t Queued() const = 0;
};
using AudioOpener = std::function<std::shared_ptr<IPcmOutput>(AudioKind, const PcmFormat&)>;

// Peak of interleaved 16-bit PCM, 0..1. `bytes` may hold a trailing partial sample.
inline float PeakOfPcm16(std::span<const std::uint8_t> bytes) {
    int peak = 0;
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        std::int16_t sample;
        std::memcpy(&sample, bytes.data() + i, sizeof(sample));
        peak = std::max(peak, sample == INT16_MIN ? 32768 : std::abs(static_cast<int>(sample)));
    }
    return static_cast<float>(peak) / 32768.0f;
}
// Scales interleaved 16-bit PCM in place. gain 1 leaves it untouched, 0 silences it.
inline void ApplyGainPcm16(std::span<std::uint8_t> bytes, float gain) {
    if (gain >= 1.0f) return;
    if (gain <= 0.0f) { std::fill(bytes.begin(), bytes.end(), std::uint8_t{0}); return; }
    for (std::size_t i = 0; i + 1 < bytes.size(); i += 2) {
        std::int16_t sample;
        std::memcpy(&sample, bytes.data() + i, sizeof(sample));
        sample = static_cast<std::int16_t>(std::clamp(static_cast<int>(static_cast<float>(sample) * gain), -32768, 32767));
        std::memcpy(bytes.data() + i, &sample, sizeof(sample));
    }
}

// Bounded FIFO between the protocol thread (writer) and an audio device thread (reader). When the
// reader falls behind, the oldest audio is dropped: a late sound is worse than a skipped one.
class PcmRingBuffer {
public:
    explicit PcmRingBuffer(std::size_t capacityBytes) : m_data(capacityBytes) {}
    void Write(std::span<const std::uint8_t> bytes) {
        std::lock_guard lock(m_mutex);
        if (bytes.size() >= m_data.size()) {
            m_dropped += m_size + bytes.size() - m_data.size();
            bytes = bytes.last(m_data.size());
            m_head = 0; m_size = 0;
        }
        const std::size_t overflow = m_size + bytes.size() > m_data.size() ? m_size + bytes.size() - m_data.size() : 0;
        if (overflow) { m_head = (m_head + overflow) % m_data.size(); m_size -= overflow; m_dropped += overflow; }
        const std::size_t tail = (m_head + m_size) % m_data.size();
        const std::size_t first = std::min(bytes.size(), m_data.size() - tail);
        std::memcpy(m_data.data() + tail, bytes.data(), first);
        std::memcpy(m_data.data(), bytes.data() + first, bytes.size() - first);
        m_size += bytes.size();
    }
    // Copies up to out.size() bytes; returns how many.
    std::size_t Read(std::span<std::uint8_t> out) {
        std::lock_guard lock(m_mutex);
        const std::size_t count = std::min(out.size(), m_size);
        const std::size_t first = std::min(count, m_data.size() - m_head);
        std::memcpy(out.data(), m_data.data() + m_head, first);
        std::memcpy(out.data() + first, m_data.data(), count - first);
        m_head = (m_head + count) % m_data.size();
        m_size -= count;
        return count;
    }
    std::size_t Size() const { std::lock_guard lock(m_mutex); return m_size; }
    void Clear() { std::lock_guard lock(m_mutex); m_head = 0; m_size = 0; }
    std::uint64_t DroppedBytes() const { std::lock_guard lock(m_mutex); return m_dropped; }
private:
    mutable std::mutex m_mutex;
    std::vector<std::uint8_t> m_data;
    std::size_t m_head{}, m_size{};
    std::uint64_t m_dropped{};
};

// The simulated car audio system: master volume in radio-style steps, mute, and per-stream
// activity for the display. Shared by the output threads and the window.
class AudioState {
public:
    static constexpr int kMaxVolume = 30;
    int Volume() const { return m_volume; }
    bool IsMuted() const { return m_isMuted; }
    void SetVolume(int volume) { m_volume = std::clamp(volume, 0, kMaxVolume); }
    // Turning the volume up while muted unmutes, like a car radio.
    void ChangeVolume(int delta) {
        if (delta > 0) m_isMuted = false;
        SetVolume(m_volume + delta);
    }
    void SetMuted(bool isMuted) { m_isMuted = isMuted; }
    void ToggleMute() { m_isMuted = !m_isMuted; }
    // Linear gain applied to the samples; squared so the steps sound even (step 30 = full scale).
    float Gain() const {
        if (m_isMuted) return 0.0f;
        const float x = static_cast<float>(m_volume) / kMaxVolume;
        return x * x;
    }
    // Output side.
    void ReportAudio(AudioKind kind, float peak, std::size_t bytes) {
        auto& meter = m_meters[static_cast<int>(kind)];
        float current = meter.peak.load();
        while (peak > current && !meter.peak.compare_exchange_weak(current, peak)) {}
        meter.bytes += bytes;
        meter.lastAudioMs = NowMs();
    }
    // Display side: the peak since the last read, and whether audio arrived recently.
    struct Meter { float peak{}; bool isActive{}; };
    Meter ReadMeter(AudioKind kind) {
        auto& meter = m_meters[static_cast<int>(kind)];
        return {meter.peak.exchange(0.0f), NowMs() - meter.lastAudioMs.load() < 500};
    }
    // Bytes the phone sent, and bytes actually handed to the sound device.
    std::uint64_t BytesPlayed(AudioKind kind) const { return m_meters[static_cast<int>(kind)].bytes; }
    void ReportRendered(AudioKind kind, std::size_t bytes) { m_meters[static_cast<int>(kind)].rendered += bytes; }
    std::uint64_t BytesRendered(AudioKind kind) const { return m_meters[static_cast<int>(kind)].rendered; }
private:
    static std::int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
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
