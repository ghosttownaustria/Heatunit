#include "audio/QueuedPcmStream.h"
#include "audio/Pcm16.h"
#include <utility>

namespace headunit {
namespace {
// What may be waiting between the protocol thread and the device; older audio is dropped.
constexpr int kRingMilliseconds = 500;
}

// A queue for `format`; the device starts (again) once `primeMilliseconds` of audio are queued, enough to bridge USB and
// scheduling jitter without adding noticeable delay.
QueuedPcmStream::QueuedPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger, int primeMilliseconds)
    : m_kind(kind), m_format(format), m_state(std::move(state)), m_logger(logger), m_ring(format.BytesPerSecond() * kRingMilliseconds / 1000),
      m_primeBytes(format.BytesPerSecond() * static_cast<std::size_t>(primeMilliseconds) / 1000)
{
}

// One line per stream when it closes: enough to tell a smooth stream from a stuttering one afterwards. The derived
// stream has stopped its device thread by now.
QueuedPcmStream::~QueuedPcmStream()
{
    if (m_hasFailed) return;
    m_logger.Write(LogLevel::Info, "AUDIO", std::string(AudioKindName(m_kind)) + " output closed: " + std::to_string(m_state->BytesRendered(m_kind)) +
        " bytes played in total, " + std::to_string(m_ring.DroppedBytes()) + " bytes dropped (late), " + std::to_string(m_underruns) + " underruns");
}

// Queues a block of the phone's audio and reports its level; after a failure of the device the audio is dropped.
void QueuedPcmStream::Write(std::span<const std::uint8_t> pcm)
{
    if (m_hasFailed || pcm.empty()) return;
    m_state->ReportAudio(m_kind, PeakOfPcm16(pcm), pcm.size());
    m_ring.Write(pcm);
}

// Drops what is queued; the device waits for a full prime again.
void QueuedPcmStream::Flush()
{
    m_ring.Clear();
    m_isPrimed = false;
}

// Bytes queued for the device.
std::size_t QueuedPcmStream::Queued() const
{
    return m_ring.Size();
}

// Which of the phone's streams this is.
AudioKind QueuedPcmStream::Kind() const
{
    return m_kind;
}

// The format of the stream.
const PcmFormat& QueuedPcmStream::Format() const
{
    return m_format;
}

// The logger of the stream, for the derived stream's own lines.
Logger& QueuedPcmStream::StreamLogger() const
{
    return m_logger;
}

// Device thread: whether playing may go on, which is once enough audio is queued after a start or an underrun.
bool QueuedPcmStream::UpdatePriming()
{
    if (!m_isPrimed && m_ring.Size() >= m_primeBytes) m_isPrimed = true;
    return m_isPrimed;
}

// Device thread: fills `block` (whole frames, no more than is queued) from the queue at the current volume.
void QueuedPcmStream::PlayQueued(std::span<std::uint8_t> block)
{
    m_ring.Read(block);
    if (m_format.bitsPerSample == 16) ApplyGainPcm16(block, m_state->Gain());
    m_state->ReportRendered(m_kind, block.size());
    m_wasPlaying = true;
}

// Device thread: the queue ran dry while playing. A little is collected again before restarting, instead of playing
// tiny fragments.
void QueuedPcmStream::NoteRanDry()
{
    if (!m_isPrimed) return;
    if (m_wasPlaying) ++m_underruns;
    m_wasPlaying = false;
    m_isPrimed = false;
}

// The device cannot be used: logged once, and the stream drops its audio from now on.
void QueuedPcmStream::Fail(const std::string& step, const std::string& reason)
{
    m_logger.Write(LogLevel::Error, "AUDIO", std::string(AudioKindName(m_kind)) + " output: " + step + " failed (" + reason + ")");
    m_hasFailed = true;
}

// Whether the device could not be used.
bool QueuedPcmStream::HasFailed() const
{
    return m_hasFailed;
}

// The line of a device that is playing: the stream's format and the device `details`.
void QueuedPcmStream::LogOpened(const std::string& details) const
{
    m_logger.Write(LogLevel::Info, "AUDIO", std::string(AudioKindName(m_kind)) + " output opened: " + std::to_string(m_format.sampleRate) + " Hz, " +
        std::to_string(m_format.channels) + " channel(s)" + details);
}
}
