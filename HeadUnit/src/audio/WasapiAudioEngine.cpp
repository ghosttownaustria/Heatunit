#include "audio/WasapiAudioEngine.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace headunit {
namespace {
using Microsoft::WRL::ComPtr;
constexpr const char* KindName(AudioKind kind) {
    return kind == AudioKind::Media ? "Media" : kind == AudioKind::Guidance ? "Guidance" : "System";
}
// Audio the device thread keeps queued before it starts playing (again): enough to bridge USB and
// scheduling jitter without adding noticeable delay.
constexpr int kPrimeMilliseconds = 60;
// What may be waiting between the protocol thread and the device; older audio is dropped.
constexpr int kRingMilliseconds = 500;

class WasapiStream final : public IPcmOutput {
public:
    WasapiStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
        : m_kind(kind), m_format(format), m_state(std::move(state)), m_logger(logger),
          m_ring(format.BytesPerSecond() * kRingMilliseconds / 1000) {
        m_thread = std::thread([this] { Run(); });
    }
    ~WasapiStream() override {
        m_isStopping = true;
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
    void Fail(const char* step, HRESULT result) {
        m_logger.Write("ERROR", "AUDIO", std::string(KindName(m_kind)) + " output: " + step + " failed (HRESULT 0x" +
            [&] { char text[16]; sprintf_s(text, "%08lX", static_cast<unsigned long>(result)); return std::string(text); }() + ")");
        m_hasFailed = true;
    }
    void Run() {
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool isComReady = SUCCEEDED(comResult);
        if (!isComReady && comResult != RPC_E_CHANGED_MODE) { Fail("COM initialization", comResult); return; }
        Play();
        if (isComReady) CoUninitialize();
    }
    void Play() {
        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (FAILED(result)) return Fail("device enumerator", result);
        ComPtr<IMMDevice> device;
        result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(result)) return Fail("default output device", result);
        ComPtr<IAudioClient> client;
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(result)) return Fail("audio client", result);
        WAVEFORMATEX wave{};
        wave.wFormatTag = WAVE_FORMAT_PCM;
        wave.nChannels = static_cast<WORD>(m_format.channels);
        wave.nSamplesPerSec = m_format.sampleRate;
        wave.wBitsPerSample = static_cast<WORD>(m_format.bitsPerSample);
        wave.nBlockAlign = static_cast<WORD>(m_format.BytesPerFrame());
        wave.nAvgBytesPerSec = static_cast<DWORD>(m_format.BytesPerSecond());
        // AUTOCONVERTPCM lets Windows resample to the device's mix format, so 16 kHz mono and
        // 48 kHz stereo both work on any output.
        result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            2000000, 0, &wave, nullptr);
        if (FAILED(result)) return Fail("stream initialization", result);
        UINT32 bufferFrames = 0;
        result = client->GetBufferSize(&bufferFrames);
        if (FAILED(result)) return Fail("buffer size", result);
        ComPtr<IAudioRenderClient> render;
        result = client->GetService(IID_PPV_ARGS(&render));
        if (FAILED(result)) return Fail("render client", result);
        result = client->Start();
        if (FAILED(result)) return Fail("start", result);
        m_logger.Write("INFO", "AUDIO", std::string(KindName(m_kind)) + " output opened: " + std::to_string(m_format.sampleRate) + " Hz, " +
            std::to_string(m_format.channels) + " channel(s), device buffer " + std::to_string(bufferFrames) + " frames");

        const std::size_t frameBytes = m_format.BytesPerFrame();
        const std::size_t primeBytes = m_format.BytesPerSecond() * kPrimeMilliseconds / 1000;
        while (!m_isStopping) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            UINT32 padding = 0;
            result = client->GetCurrentPadding(&padding);
            if (FAILED(result)) { Fail("device state (output device removed?)", result); break; }
            const std::size_t queued = m_ring.Size();
            if (!m_isPrimed) {
                if (queued < primeBytes) continue;
                m_isPrimed = true;
            }
            const UINT32 frames = static_cast<UINT32>(std::min<std::size_t>(bufferFrames - padding, queued / frameBytes));
            if (frames == 0) {
                // The device has played everything and nothing new arrived: collect a little again
                // before restarting, instead of playing tiny fragments.
                if (padding == 0) { if (m_wasPlaying) ++m_underruns; m_wasPlaying = false; m_isPrimed = false; }
                continue;
            }
            BYTE* data = nullptr;
            result = render->GetBuffer(frames, &data);
            if (FAILED(result)) { Fail("device buffer", result); break; }
            const std::span<std::uint8_t> block(data, static_cast<std::size_t>(frames) * frameBytes);
            m_ring.Read(block);
            if (m_format.bitsPerSample == 16) ApplyGainPcm16(block, m_state->Gain());
            render->ReleaseBuffer(frames, 0);
            m_state->ReportRendered(m_kind, block.size());
            m_wasPlaying = true;
        }
        client->Stop();
    }
    AudioKind m_kind;
    PcmFormat m_format;
    std::shared_ptr<AudioState> m_state;
    Logger& m_logger;
    PcmRingBuffer m_ring;
    std::atomic_bool m_isStopping{false}, m_isPrimed{false}, m_hasFailed{false};
    bool m_wasPlaying{false};       // render thread only
    unsigned m_underruns{0};        // render thread only, read after the join
    std::thread m_thread;
};
}

std::shared_ptr<IPcmOutput> WasapiAudioEngine::Open(AudioKind kind, const PcmFormat& format)
{
    if (format.bitsPerSample != 16 || format.channels == 0 || format.sampleRate == 0) {
        m_logger.Write("ERROR", "AUDIO", "Unsupported audio format from the phone");
        return nullptr;
    }
    return std::make_shared<WasapiStream>(kind, format, m_state, m_logger);
}
}
