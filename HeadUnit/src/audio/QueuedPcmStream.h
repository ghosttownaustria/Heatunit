#pragma once
#include "audio/AudioState.h"
#include "audio/IPcmOutput.h"
#include "audio/PcmRingBuffer.h"
#include "logging/Logger.h"
#include <atomic>
#include <memory>
#include <string>

namespace headunit {
// The common part of the platform audio streams: the queue between the protocol thread and the sound device, the level
// reports to the AudioState, the priming and underrun bookkeeping, and the log lines. A derived stream feeds the device
// from its own thread and must stop that thread in its destructor.
class QueuedPcmStream : public IPcmOutput {
public:
    QueuedPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger, int primeMilliseconds);
    ~QueuedPcmStream() override;

    void Write(std::span<const std::uint8_t> pcm) override;
    void Flush() override;
    std::size_t Queued() const override;

protected:
    AudioKind Kind() const;
    const PcmFormat& Format() const;
    Logger& StreamLogger() const;
    bool UpdatePriming();
    void PlayQueued(std::span<std::uint8_t> block);
    void NoteRanDry();
    void Fail(const std::string& step, const std::string& reason);
    bool HasFailed() const;
    void LogOpened(const std::string& details) const;

private:
    AudioKind m_kind;
    PcmFormat m_format;
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
    PcmRingBuffer m_ring;
    std::size_t m_primeBytes;
    std::atomic_bool m_isPrimed{false};
    std::atomic_bool m_hasFailed{false};
    bool m_wasPlaying{false};   // device thread only
    unsigned m_underruns{0};    // device thread only, read after it has ended
};
}
