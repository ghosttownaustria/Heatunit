#pragma once
#include "audio/AudioState.h"
#include "audio/IPcmOutput.h"
#include "logging/Logger.h"
#include <memory>

// The build compiles exactly one implementation: WasapiPcmStream.cpp (Windows) or MiniaudioPcmStream.cpp (every other
// platform, and Windows with HEADUNIT_AUDIO_BACKEND=miniaudio).
namespace headunit {
std::shared_ptr<IPcmOutput> OpenPlatformPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger);
}
