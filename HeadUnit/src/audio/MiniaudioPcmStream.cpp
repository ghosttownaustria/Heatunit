#include "audio/MiniaudioPcmStream.h"
#include "audio/MiniaudioConfig.h"
#include "audio/PlatformPcmStream.h"
#include "platform/Environment.h"
#include <miniaudio.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

namespace headunit {
namespace {
// Audio the device callback wants queued before it starts playing (again).
constexpr int kPrimeMilliseconds = 100;
constexpr int kPeriodMilliseconds = 20;
constexpr int kPeriods = 3;
// The sound systems to try, in this order. The null backend is left out on purpose: it "works" without playing
// anything, which would hide that there is no sound output.
constexpr ma_backend kBackends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_coreaudio, ma_backend_pulseaudio,
    ma_backend_alsa, ma_backend_jack, ma_backend_sndio, ma_backend_audio4, ma_backend_oss};

// `text` in lower case, for comparing device names.
std::string Lower(std::string text)
{
    for (auto& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

// The sound system's callback thread asks for `frames` frames.
void OnData(ma_device* device, void* output, const void*, ma_uint32 frames)
{
    static_cast<MiniaudioPcmStream*>(device->pUserData)->Render(static_cast<std::uint8_t*>(output), frames);
}

// The sound system reports a change of the device; only a stop matters.
void OnNotification(const ma_device_notification* notification)
{
    if (notification->type == ma_device_notification_type_stopped)
        static_cast<MiniaudioPcmStream*>(notification->pDevice->pUserData)->NoteDeviceStopped();
}

// The output device HEADUNIT_AUDIO_DEVICE names (part of its name, any case); the default device when it names none.
const ma_device_id* SelectDevice(ma_context& context, Logger& logger)
{
    const auto wanted = GetEnv("HEADUNIT_AUDIO_DEVICE");
    if (!wanted || wanted->empty()) return nullptr;
    ma_device_info* devices = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&context, &devices, &count, nullptr, nullptr) == MA_SUCCESS) {
        for (ma_uint32 index = 0; index < count; ++index) {
            if (Lower(devices[index].name).find(Lower(*wanted)) != std::string::npos) return &devices[index].id;
        }
    }
    logger.Write(LogLevel::Warning, "AUDIO", "HEADUNIT_AUDIO_DEVICE=" + *wanted + " matches no output device; using the default");
    return nullptr;
}
}

// Opens the device on a thread of its own, since this constructor runs on the protocol thread.
MiniaudioPcmStream::MiniaudioPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
    : QueuedPcmStream(kind, format, std::move(state), logger, kPrimeMilliseconds)
{
    m_thread = std::thread([this] { Run(); });
}

// Wakes the device thread, which closes the device (stopping the callbacks) before the queue goes away.
MiniaudioPcmStream::~MiniaudioPcmStream()
{
    {
        std::lock_guard lock(m_mutex);
        m_isStopping = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

// Callback thread: fills `frames` frames, with silence where nothing is queued.
void MiniaudioPcmStream::Render(std::uint8_t* output, unsigned frames)
{
    const std::size_t frameBytes = Format().BytesPerFrame();
    const std::size_t wanted = static_cast<std::size_t>(frames) * frameBytes;
    std::size_t delivered = 0;
    if (UpdatePriming()) {
        delivered = std::min(wanted, Queued() / frameBytes * frameBytes);
        if (delivered > 0) PlayQueued({output, delivered});
    }
    if (delivered < wanted) {
        std::memset(output + delivered, 0, wanted - delivered);
        NoteRanDry();
    }
}

// Callback thread: the sound system stopped the device by itself (not the stream closing it).
void MiniaudioPcmStream::NoteDeviceStopped()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_isStopping) return;
    }
    StreamLogger().Write(LogLevel::Warning, "AUDIO", std::string(AudioKindName(Kind())) + " output was stopped by the sound system (output device removed?)");
}

// Device thread: opens the sound system and the device, starts it, and keeps it open until the stream closes.
void MiniaudioPcmStream::Run()
{
    ma_context_config contextConfig = ma_context_config_init();
    contextConfig.pulse.pApplicationName = "HeadUnit";
    ma_context context;
    if (const ma_result result = ma_context_init(kBackends, static_cast<ma_uint32>(std::size(kBackends)), &contextConfig, &context); result != MA_SUCCESS)
        return FailWith("sound system initialization (no usable PulseAudio, ALSA or other sound system)", result);
    const PcmFormat& format = Format();
    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_s16;
    config.playback.channels = format.channels;
    config.playback.pDeviceID = SelectDevice(context, StreamLogger());
    config.sampleRate = format.sampleRate;
    config.periodSizeInMilliseconds = kPeriodMilliseconds;
    config.periods = kPeriods;
    config.dataCallback = &OnData;
    config.notificationCallback = &OnNotification;
    config.pUserData = this;
    config.pulse.pStreamNamePlayback = AudioKindName(Kind());
    ma_device device;
    ma_result result = ma_device_init(&context, &config, &device);
    if (result != MA_SUCCESS) {
        ma_context_uninit(&context);
        return FailWith("output device", result);
    }
    result = ma_device_start(&device);
    if (result != MA_SUCCESS) {
        ma_device_uninit(&device);
        ma_context_uninit(&context);
        return FailWith("start", result);
    }
    LogOpened(std::string(" via ") + ma_get_backend_name(context.backend) + " on '" + device.playback.name + "', device buffer " +
        std::to_string(device.playback.internalPeriodSizeInFrames * device.playback.internalPeriods) + " frames");
    {
        std::unique_lock lock(m_mutex);
        m_wake.wait(lock, [this] { return m_isStopping; });
    }
    ma_device_uninit(&device);   // stops the callback thread before anything it uses goes away
    ma_context_uninit(&context);
}

// Device thread: `step` failed with a miniaudio result.
void MiniaudioPcmStream::FailWith(const char* step, int result)
{
    Fail(step, ma_result_description(static_cast<ma_result>(result)));
}

// Opens one stream through miniaudio.
std::shared_ptr<IPcmOutput> OpenPlatformPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
{
    return std::make_shared<MiniaudioPcmStream>(kind, format, std::move(state), logger);
}
}
