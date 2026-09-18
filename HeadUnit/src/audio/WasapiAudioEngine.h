#pragma once
#include "audio/AudioTypes.h"
#include "logging/Logger.h"
#include <memory>

namespace headunit {
// Plays the phone's PCM streams on the default Windows output device (WASAPI, shared mode, so other
// programs keep their sound). Volume and mute come from the shared AudioState, and every stream
// reports its level back to it for the on-screen display.
class WasapiAudioEngine {
public:
    WasapiAudioEngine(std::shared_ptr<AudioState> state, Logger& logger) : m_state(std::move(state)), m_logger(logger) {}
    // The opener a session uses; valid as long as this engine lives.
    AudioOpener Opener() {
        return [this](AudioKind kind, const PcmFormat& format) { return Open(kind, format); };
    }
private:
    std::shared_ptr<IPcmOutput> Open(AudioKind kind, const PcmFormat& format);
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
};
}
