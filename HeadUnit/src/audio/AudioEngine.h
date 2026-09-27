#pragma once
#include "audio/AudioTypes.h"
#include "logging/Logger.h"
#include <memory>

namespace headunit {
// Plays the phone's PCM streams on the sound output of this platform without taking it over (other programs
// keep their sound). Volume and mute come from the shared AudioState, and every stream reports its level back
// to it for the on-screen display.
class IAudioEngine {
public:
    virtual ~IAudioEngine() = default;
    // The opener a session uses; valid as long as this engine lives.
    virtual AudioOpener Opener() = 0;
};
// WASAPI on Windows, miniaudio elsewhere (PulseAudio and PipeWire's pulse server, ALSA, JACK; whichever the
// system has). A Windows build can use miniaudio too by defining HEADUNIT_AUDIO_MINIAUDIO.
std::unique_ptr<IAudioEngine> CreateAudioEngine(std::shared_ptr<AudioState> state, Logger& logger);
}
