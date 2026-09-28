#pragma once
#include "audio/QueuedPcmStream.h"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

namespace headunit {
// One of the phone's streams through miniaudio, on its own device connection, which the sound system mixes with
// everything else. miniaudio picks the sound system at run time, so nothing but the program itself is needed to build or
// start it: PulseAudio (which PipeWire serves as well), then ALSA, then JACK; on Windows WASAPI first.
// HEADUNIT_AUDIO_DEVICE=<part of a device name> selects another output device than the default. Opening the device can
// take a while (or hang on a sound server that does not answer), so a thread of its own opens it and keeps it open.
class MiniaudioPcmStream final : public QueuedPcmStream {
public:
    MiniaudioPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger);
    ~MiniaudioPcmStream() override;

    void Render(std::uint8_t* output, unsigned frames);
    void NoteDeviceStopped();

private:
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_isStopping{false};   // under m_mutex
    std::thread m_thread;

    void Run();
    void FailWith(const char* step, int result);
};
}
