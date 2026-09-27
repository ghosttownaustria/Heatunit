#include "audio/MiniaudioEngine.h"
#include "audio/MiniaudioConfig.h"
#include "platform/Environment.h"
#include <miniaudio.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <cstring>
#include <iterator>
#include <mutex>
#include <string>
#include <thread>

namespace headunit {
namespace {
constexpr const char* KindName(AudioKind kind) {
    return kind == AudioKind::Media ? "Media" : kind == AudioKind::Guidance ? "Guidance" : "System";
}
// Audio the device callback wants queued before it starts playing (again): enough to bridge USB and
// scheduling jitter of the protocol thread without adding noticeable delay.
constexpr int kPrimeMilliseconds = 100;
// What may be waiting between the protocol thread and the device; older audio is dropped.
constexpr int kRingMilliseconds = 500;
constexpr int kPeriodMilliseconds = 20;
constexpr int kPeriods = 3;
// The sound systems to try, in this order. The null backend is left out on purpose: it "works" without
// playing anything, which would hide that there is no sound output.
constexpr ma_backend kBackends[] = {ma_backend_wasapi, ma_backend_dsound, ma_backend_winmm, ma_backend_coreaudio,
    ma_backend_pulseaudio, ma_backend_alsa, ma_backend_jack, ma_backend_sndio, ma_backend_audio4, ma_backend_oss};

std::string Lower(std::string text) {
    for (auto& character : text) character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return text;
}

class MiniaudioStream final : public IPcmOutput {
public:
    MiniaudioStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
        : m_kind(kind), m_format(format), m_state(std::move(state)), m_logger(logger),
          m_ring(format.BytesPerSecond() * kRingMilliseconds / 1000),
          m_primeBytes(format.BytesPerSecond() * kPrimeMilliseconds / 1000) {
        // Opening the device can take a while (or hang on a sound server that does not answer), and this
        // constructor runs on the protocol thread: the device gets its own thread, as with WASAPI.
        m_thread = std::thread([this] { Run(); });
    }
    ~MiniaudioStream() override {
        { std::lock_guard lock(m_mutex); m_isStopping = true; }
        m_wake.notify_all();
        if (m_thread.joinable()) m_thread.join();
        // One line per stream: enough to tell a smooth stream from a stuttering one afterwards.
        if (!m_hasFailed) m_logger.Write("INFO", "AUDIO", std::string(KindName(m_kind)) + " output closed: " + std::to_string(m_state->BytesRendered(m_kind)) +
            " bytes played in total, " + std::to_string(m_ring.DroppedBytes()) + " bytes dropped (late), " + std::to_string(m_underruns) + " underruns");
    }
    void Write(std::span<const std::uint8_t> pcm) override {
        if (m_hasFailed || pcm.empty()) return;
        m_state->ReportAudio(m_kind, PeakOfPcm16(pcm), pcm.size());
        m_ring.Write(pcm);
    }
    void Flush() override {
        m_ring.Clear();
        m_isPrimed = false;
    }
    std::size_t Queued() const override { return m_ring.Size(); }
private:
    void Fail(const char* step, ma_result result) {
        m_logger.Write("ERROR", "AUDIO", std::string(KindName(m_kind)) + " output: " + step + " failed (" + ma_result_description(result) + ")");
        m_hasFailed = true;
    }
    void Run() {
        ma_context_config contextConfig = ma_context_config_init();
        contextConfig.pulse.pApplicationName = "HeadUnit";
        ma_context context;
        if (const ma_result result = ma_context_init(kBackends, static_cast<ma_uint32>(std::size(kBackends)), &contextConfig, &context); result != MA_SUCCESS)
            return Fail("sound system initialization (no usable PulseAudio, ALSA or other sound system)", result);
        const ma_device_id* selected = nullptr;
        if (const auto wanted = GetEnv("HEADUNIT_AUDIO_DEVICE"); wanted && !wanted->empty()) {
            ma_device_info* devices = nullptr;
            ma_uint32 count = 0;
            if (ma_context_get_devices(&context, &devices, &count, nullptr, nullptr) == MA_SUCCESS)
                for (ma_uint32 i = 0; i < count && !selected; ++i)
                    if (Lower(devices[i].name).find(Lower(*wanted)) != std::string::npos) selected = &devices[i].id;
            if (!selected) m_logger.Write("WARN", "AUDIO", "HEADUNIT_AUDIO_DEVICE=" + *wanted + " matches no output device; using the default");
        }
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_s16;
        config.playback.channels = m_format.channels;
        config.playback.pDeviceID = selected;
        config.sampleRate = m_format.sampleRate;
        config.periodSizeInMilliseconds = kPeriodMilliseconds;
        config.periods = kPeriods;
        config.dataCallback = &MiniaudioStream::OnData;
        config.notificationCallback = &MiniaudioStream::OnNotification;
        config.pUserData = this;
        config.pulse.pStreamNamePlayback = KindName(m_kind);
        ma_device device;
        ma_result result = ma_device_init(&context, &config, &device);
        if (result != MA_SUCCESS) { ma_context_uninit(&context); return Fail("output device", result); }
        result = ma_device_start(&device);
        if (result != MA_SUCCESS) { ma_device_uninit(&device); ma_context_uninit(&context); return Fail("start", result); }
        m_logger.Write("INFO", "AUDIO", std::string(KindName(m_kind)) + " output opened: " + std::to_string(m_format.sampleRate) + " Hz, " +
            std::to_string(m_format.channels) + " channel(s) via " + ma_get_backend_name(context.backend) + " on '" + device.playback.name +
            "', device buffer " + std::to_string(device.playback.internalPeriodSizeInFrames * device.playback.internalPeriods) + " frames");
        { std::unique_lock lock(m_mutex); m_wake.wait(lock, [this] { return m_isStopping; }); }
        ma_device_uninit(&device);   // stops the callback thread before anything it uses goes away
        ma_context_uninit(&context);
    }
    static void OnData(ma_device* device, void* output, const void*, ma_uint32 frames) {
        static_cast<MiniaudioStream*>(device->pUserData)->Render(static_cast<std::uint8_t*>(output), frames);
    }
    static void OnNotification(const ma_device_notification* notification) {
        if (notification->type == ma_device_notification_type_stopped)
            static_cast<MiniaudioStream*>(notification->pDevice->pUserData)->OnStopped();
    }
    void OnStopped() {
        { std::lock_guard lock(m_mutex); if (m_isStopping) return; }
        m_logger.Write("WARN", "AUDIO", std::string(KindName(m_kind)) + " output was stopped by the sound system (output device removed?)");
    }
    // Runs on the sound system's callback thread: fills `frames` frames, with silence where nothing is queued.
    void Render(std::uint8_t* output, ma_uint32 frames) {
        const std::size_t frameBytes = m_format.BytesPerFrame();
        const std::size_t wanted = static_cast<std::size_t>(frames) * frameBytes;
        std::size_t delivered = 0;
        if (!m_isPrimed && m_ring.Size() >= m_primeBytes) m_isPrimed = true;
        if (m_isPrimed) {
            const std::size_t count = std::min(wanted, m_ring.Size() / frameBytes * frameBytes);
            if (count > 0) {
                const std::span<std::uint8_t> block(output, count);
                m_ring.Read(block);
                if (m_format.bitsPerSample == 16) ApplyGainPcm16(block, m_state->Gain());
                m_state->ReportRendered(m_kind, count);
                delivered = count;
                m_wasPlaying = true;
            }
        }
        if (delivered < wanted) {
            std::memset(output + delivered, 0, wanted - delivered);
            // The queue ran dry while playing: collect a little again before restarting, instead of playing
            // tiny fragments.
            if (m_isPrimed) { if (m_wasPlaying) ++m_underruns; m_wasPlaying = false; m_isPrimed = false; }
        }
    }
    AudioKind m_kind;
    PcmFormat m_format;
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
    PcmRingBuffer m_ring;
    std::size_t m_primeBytes;
    std::atomic_bool m_isPrimed{false}, m_hasFailed{false};
    bool m_wasPlaying{false};       // callback thread only
    unsigned m_underruns{0};        // callback thread only, read after the join
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_isStopping{false};       // under m_mutex
    std::thread m_thread;
};
}

std::shared_ptr<IPcmOutput> MiniaudioEngine::Open(AudioKind kind, const PcmFormat& format)
{
    if (format.bitsPerSample != 16 || format.channels == 0 || format.sampleRate == 0) {
        m_logger.Write("ERROR", "AUDIO", "Unsupported audio format from the phone");
        return nullptr;
    }
    return std::make_shared<MiniaudioStream>(kind, format, m_state, m_logger);
}
}
