#pragma once
#include "audio/QueuedPcmStream.h"
#include <atomic>
#include <thread>

namespace headunit {
// One of the phone's streams on the default Windows output device (WASAPI, shared mode, so other programs keep their
// sound). Windows resamples to the device's mix format, so 16 kHz mono and 48 kHz stereo both work on any output. A
// thread of its own opens the device and feeds it every 10 ms.
class WasapiPcmStream final : public QueuedPcmStream {
public:
    WasapiPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger);
    ~WasapiPcmStream() override;

private:
    std::atomic_bool m_isStopping{false};
    std::thread m_thread;

    void Run();
    void Play();
    void FailWith(const char* step, long result);
};
}
