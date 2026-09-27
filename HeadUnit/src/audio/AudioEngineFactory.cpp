#include "audio/AudioEngine.h"
#if defined(_WIN32) && !defined(HEADUNIT_AUDIO_MINIAUDIO)
#include "audio/WasapiAudioEngine.h"
#else
#include "audio/MiniaudioEngine.h"
#endif

namespace headunit {
std::unique_ptr<IAudioEngine> CreateAudioEngine(std::shared_ptr<AudioState> state, Logger& logger)
{
#if defined(_WIN32) && !defined(HEADUNIT_AUDIO_MINIAUDIO)
    return std::make_unique<WasapiAudioEngine>(std::move(state), logger);
#else
    return std::make_unique<MiniaudioEngine>(std::move(state), logger);
#endif
}
}
