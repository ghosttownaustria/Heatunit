#include "audio/AudioClock.h"
#include <chrono>

namespace headunit {
// Milliseconds of a monotonic clock: the time base of the audio meters and of who has the media sound.
std::int64_t SteadyNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}
