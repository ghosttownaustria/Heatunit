#pragma once
#include "audio/AudioEngine.h"

namespace headunit {
// Output through miniaudio. It picks the sound system at run time, so nothing but the program itself is
// needed to build or start it: PulseAudio (which PipeWire serves as well), then ALSA, then JACK; on Windows
// WASAPI first. HEADUNIT_AUDIO_DEVICE=<part of a device name> selects another output device than the default.
// Each stream plays on its own device connection, which the sound system mixes with everything else.
class MiniaudioEngine final : public IAudioEngine {
public:
    MiniaudioEngine(std::shared_ptr<AudioState> state, Logger& logger) : m_state(std::move(state)), m_logger(logger) {}
    AudioOpener Opener() override {
        return [this](AudioKind kind, const PcmFormat& format) { return Open(kind, format); };
    }
private:
    std::shared_ptr<IPcmOutput> Open(AudioKind kind, const PcmFormat& format);
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
};
}
