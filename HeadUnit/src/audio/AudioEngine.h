#pragma once
#include "audio/AudioState.h"
#include "audio/IPcmOutput.h"
#include "logging/Logger.h"
#include <memory>

namespace headunit {
// Plays the phone's PCM streams on this platform's sound output without taking it over (other programs keep their
// sound): WASAPI on Windows, miniaudio elsewhere (PulseAudio and PipeWire's pulse server, ALSA, JACK; whichever the system
// has). Volume and mute come from the shared AudioState, and every stream reports its level back to it for the on-screen
// display.
class AudioEngine {
public:
    AudioEngine(std::shared_ptr<AudioState> state, Logger& logger);

    AudioOpener Opener();

private:
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;

    std::shared_ptr<IPcmOutput> Open(AudioKind kind, const PcmFormat& format);
};
}
