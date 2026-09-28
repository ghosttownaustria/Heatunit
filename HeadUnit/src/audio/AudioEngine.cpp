#include "audio/AudioEngine.h"
#include "audio/PlatformPcmStream.h"
#include <utility>

namespace headunit {
// An engine that plays at the volume of `state`.
AudioEngine::AudioEngine(std::shared_ptr<AudioState> state, Logger& logger) : m_state(std::move(state)), m_logger(logger)
{
}

// The opener a session (or the radio's player) uses; valid as long as this engine lives.
AudioOpener AudioEngine::Opener()
{
    return [this](AudioKind kind, const PcmFormat& format) { return Open(kind, format); };
}

// Opens an output for one stream; only 16-bit PCM is played.
std::shared_ptr<IPcmOutput> AudioEngine::Open(AudioKind kind, const PcmFormat& format)
{
    if (format.bitsPerSample != 16 || format.channels == 0 || format.sampleRate == 0) {
        m_logger.Write(LogLevel::Error, "AUDIO", "Unsupported audio format from the phone");
        return nullptr;
    }
    return OpenPlatformPcmStream(kind, format, m_state, m_logger);
}
}
