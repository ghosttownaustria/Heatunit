#include "audio/WasapiPcmStream.h"
#include "audio/PlatformPcmStream.h"
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>

#pragma comment(lib, "ole32.lib")

namespace headunit {
namespace {
using Microsoft::WRL::ComPtr;
// Audio the device thread keeps queued before it starts playing (again).
constexpr int kPrimeMilliseconds = 60;
// The shared-mode buffer the stream asks for, in 100 ns units.
constexpr REFERENCE_TIME kBufferDuration = 2000000;
constexpr auto kPollInterval = std::chrono::milliseconds(10);
}

// Opens the device on a thread of its own, so that a slow device never blocks the protocol thread.
WasapiPcmStream::WasapiPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
    : QueuedPcmStream(kind, format, std::move(state), logger, kPrimeMilliseconds)
{
    m_thread = std::thread([this] { Run(); });
}

// Stops the device thread before the queue goes away.
WasapiPcmStream::~WasapiPcmStream()
{
    m_isStopping = true;
    if (m_thread.joinable()) m_thread.join();
}

// Device thread: initializes COM for itself and plays until the stream is stopped.
void WasapiPcmStream::Run()
{
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool isComReady = SUCCEEDED(comResult);
    if (!isComReady && comResult != RPC_E_CHANGED_MODE) {
        FailWith("COM initialization", comResult);
        return;
    }
    Play();
    if (isComReady) CoUninitialize();
}

// Device thread: opens the default output device and feeds it from the queue every 10 ms, as much as fits.
void WasapiPcmStream::Play()
{
    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(result)) return FailWith("device enumerator", result);
    ComPtr<IMMDevice> device;
    result = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
    if (FAILED(result)) return FailWith("default output device", result);
    ComPtr<IAudioClient> client;
    result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(result)) return FailWith("audio client", result);
    const PcmFormat& format = Format();
    WAVEFORMATEX wave{};
    wave.wFormatTag = WAVE_FORMAT_PCM;
    wave.nChannels = static_cast<WORD>(format.channels);
    wave.nSamplesPerSec = format.sampleRate;
    wave.wBitsPerSample = static_cast<WORD>(format.bitsPerSample);
    wave.nBlockAlign = static_cast<WORD>(format.BytesPerFrame());
    wave.nAvgBytesPerSec = static_cast<DWORD>(format.BytesPerSecond());
    result = client->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY, kBufferDuration, 0,
        &wave, nullptr);
    if (FAILED(result)) return FailWith("stream initialization", result);
    UINT32 bufferFrames = 0;
    result = client->GetBufferSize(&bufferFrames);
    if (FAILED(result)) return FailWith("buffer size", result);
    ComPtr<IAudioRenderClient> render;
    result = client->GetService(IID_PPV_ARGS(&render));
    if (FAILED(result)) return FailWith("render client", result);
    result = client->Start();
    if (FAILED(result)) return FailWith("start", result);
    LogOpened(", device buffer " + std::to_string(bufferFrames) + " frames");

    const std::size_t frameBytes = format.BytesPerFrame();
    while (!m_isStopping) {
        std::this_thread::sleep_for(kPollInterval);
        UINT32 padding = 0;
        result = client->GetCurrentPadding(&padding);
        if (FAILED(result)) {
            FailWith("device state (output device removed?)", result);
            break;
        }
        if (!UpdatePriming()) continue;
        const auto frames = static_cast<UINT32>(std::min<std::size_t>(bufferFrames - padding, Queued() / frameBytes));
        if (frames == 0) {
            // The device has played everything and nothing new arrived.
            if (padding == 0) NoteRanDry();
            continue;
        }
        BYTE* data = nullptr;
        result = render->GetBuffer(frames, &data);
        if (FAILED(result)) {
            FailWith("device buffer", result);
            break;
        }
        PlayQueued({data, static_cast<std::size_t>(frames) * frameBytes});
        render->ReleaseBuffer(frames, 0);
    }
    client->Stop();
}

// Device thread: `step` failed with an HRESULT.
void WasapiPcmStream::FailWith(const char* step, long result)
{
    char text[16]{};
    std::snprintf(text, sizeof(text), "%08lX", static_cast<unsigned long>(result));
    Fail(step, std::string("HRESULT 0x") + text);
}

// Opens one stream on the default Windows output device.
std::shared_ptr<IPcmOutput> OpenPlatformPcmStream(AudioKind kind, const PcmFormat& format, std::shared_ptr<AudioState> state, Logger& logger)
{
    return std::make_shared<WasapiPcmStream>(kind, format, std::move(state), logger);
}
}
