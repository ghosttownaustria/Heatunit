#pragma once
#include "audio/AudioEngine.h"

namespace headunit {
// Plays the phone's PCM streams on the default Windows output device (WASAPI, shared mode, so other
// programs keep their sound).
class WasapiAudioEngine final : public IAudioEngine {
public:
    WasapiAudioEngine(std::shared_ptr<AudioState> state, Logger& logger) : m_state(std::move(state)), m_logger(logger) {}
    AudioOpener Opener() override {
        return [this](AudioKind kind, const PcmFormat& format) { return Open(kind, format); };
    }
private:
    std::shared_ptr<IPcmOutput> Open(AudioKind kind, const PcmFormat& format);
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
};
}
