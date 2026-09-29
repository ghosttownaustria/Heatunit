#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "audio/AudioClock.h"
#include "audio/AudioState.h"
#include "audio/IPcmOutput.h"
#include "audio/MediaActivity.h"
#include "audio/Pcm16.h"
#include "audio/PcmRingBuffer.h"
#include "audio/WatchedOutput.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

using namespace headunit;

namespace {
// The bytes of `values`.
std::vector<std::uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> bytes;
    for (const int value : values) bytes.push_back(static_cast<std::uint8_t>(value));
    return bytes;
}
// 16-bit little-endian PCM of `samples`.
std::vector<std::uint8_t> Pcm(std::initializer_list<int> samples) {
    std::vector<std::uint8_t> bytes;
    for (const int sample : samples) {
        const auto value = static_cast<std::int16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }
    return bytes;
}
// The sample at `index` of 16-bit little-endian PCM.
std::int16_t SampleAt(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    return static_cast<std::int16_t>(bytes[index * 2] | (bytes[index * 2 + 1] << 8));
}

// The ring buffer keeps the order of the audio and drops the oldest bytes when it overflows.
void TestRingBuffer() {
    PcmRingBuffer ring(8);
    ring.Write(Bytes({1, 2, 3, 4, 5}));
    std::vector<std::uint8_t> out(3);
    Check(ring.Read(out) == 3 && out == Bytes({1, 2, 3}), "Ring buffer did not return the oldest bytes first");
    ring.Write(Bytes({6, 7, 8, 9, 10}));  // wraps around the end of the storage
    Check(ring.Size() == 7, "Ring buffer size wrong after wrapping");
    out.assign(7, 0);
    Check(ring.Read(out) == 7 && out == Bytes({4, 5, 6, 7, 8, 9, 10}), "Ring buffer lost ordering across the wrap");
    Check(ring.Size() == 0 && ring.Read(out) == 0, "An empty ring buffer returned data");

    // A slow reader loses the oldest audio, never the newest.
    ring.Write(Bytes({1, 2, 3, 4, 5, 6}));
    ring.Write(Bytes({7, 8, 9, 10}));
    Check(ring.Size() == 8 && ring.DroppedBytes() == 2, "Overflow did not drop exactly the excess");
    out.assign(8, 0);
    ring.Read(out);
    Check(out == Bytes({3, 4, 5, 6, 7, 8, 9, 10}), "Overflow dropped the wrong end");

    // A block larger than the buffer keeps its tail.
    ring.Write(Bytes({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12}));
    out.assign(8, 0);
    ring.Read(out);
    Check(out == Bytes({5, 6, 7, 8, 9, 10, 11, 12}), "An oversized block did not keep its newest bytes");
    ring.Write(Bytes({1, 2}));
    ring.Clear();
    Check(ring.Size() == 0, "Clear left audio in the buffer");
}

// The peak and the gain of 16-bit PCM.
void TestPcmMath() {
    const auto loud = Pcm({100, -20000, 500, 32767});
    Check(PeakOfPcm16(loud) > 0.999f, "Peak of full-scale audio is wrong");
    Check(PeakOfPcm16(Pcm({0, 0, 0, 0})) == 0.0f, "Silence has a peak");
    Check(PeakOfPcm16(Pcm({-32768})) == 1.0f, "Most negative sample is not full scale");
    const auto odd = Bytes({0x00, 0x40, 0x7f});  // trailing half sample is ignored
    Check(PeakOfPcm16(odd) > 0.49f && PeakOfPcm16(odd) < 0.51f, "A partial trailing sample changed the peak");

    auto samples = Pcm({1000, -1000, 32767, -32768});
    ApplyGainPcm16(samples, 0.5f);
    Check(SampleAt(samples, 0) == 500 && SampleAt(samples, 1) == -500, "Half gain is wrong");
    Check(SampleAt(samples, 2) == 16383 && SampleAt(samples, 3) == -16384, "Half gain of full scale is wrong");
    auto untouched = Pcm({1234, -4321});
    ApplyGainPcm16(untouched, 1.0f);
    Check(SampleAt(untouched, 0) == 1234 && SampleAt(untouched, 1) == -4321, "Unity gain changed the audio");
    ApplyGainPcm16(untouched, 0.0f);
    Check(SampleAt(untouched, 0) == 0 && SampleAt(untouched, 1) == 0, "Zero gain did not silence the audio");
}

// Volume, mute and the level meters of the streams.
void TestAudioState() {
    AudioState state;
    state.SetVolume(999);
    Check(state.Volume() == AudioState::kMaxVolume && state.Gain() == 1.0f, "Volume is not limited to full scale");
    state.SetVolume(-5);
    Check(state.Volume() == 0 && state.Gain() == 0.0f, "Volume is not limited to zero");
    state.SetVolume(10);
    const float low = state.Gain();
    state.SetVolume(20);
    Check(low > 0.0f && state.Gain() > low && state.Gain() < 1.0f, "Volume steps do not raise the gain");
    state.ToggleMute();
    Check(state.IsMuted() && state.Gain() == 0.0f, "Mute does not silence");
    state.ChangeVolume(-1);
    Check(state.IsMuted(), "Turning the volume down cancelled mute");
    state.ChangeVolume(+1);
    Check(!state.IsMuted() && state.Volume() == 20, "Turning the volume up while muted did not unmute");
    Check(!state.IsMicrophoneMuted(), "The microphone starts muted");
    state.ToggleMicrophoneMute();
    Check(state.IsMicrophoneMuted() && !state.IsMuted() && state.Gain() > 0.0f, "Muting the microphone touched the sound");
    state.ToggleMicrophoneMute();
    Check(!state.IsMicrophoneMuted(), "The microphone did not come back");

    Check(!state.ReadMeter(AudioKind::Media).isActive, "A stream that never played is active");
    state.ReportAudio(AudioKind::Media, 0.4f, 100);
    state.ReportAudio(AudioKind::Media, 0.9f, 100);
    state.ReportAudio(AudioKind::Media, 0.2f, 100);
    const auto meter = state.ReadMeter(AudioKind::Media);
    Check(meter.isActive && meter.peak > 0.89f && meter.peak < 0.91f, "Meter did not keep the highest peak");
    Check(state.ReadMeter(AudioKind::Media).peak == 0.0f, "Reading the meter did not reset the peak");
    Check(state.BytesPlayed(AudioKind::Media) == 300 && state.BytesPlayed(AudioKind::System) == 0, "Played bytes are wrong");
    Check(!state.ReadMeter(AudioKind::Guidance).isActive, "Streams share one meter");
}

// An output that counts what it is given.
struct CountingOutput final : IPcmOutput {
    std::size_t bytes{};
    bool isFlushed{};
    void Write(std::span<const std::uint8_t> pcm) override { bytes += pcm.size(); }
    void Flush() override { isFlushed = true; }
    std::size_t Queued() const override { return 7; }
};

// One source sounds: the phone's music and the radio's own player, whichever started last.
void TestAudioFocus() {
    MediaActivity phone;
    Check(!phone.IsSounding(10000), "The phone sounds before any audio came");
    phone.NoteAudio(10000);
    phone.NoteAudio(10020);
    phone.NoteAudio(10400);   // a running stream: small gaps
    Check(phone.IsSounding(10500) && phone.StartMs() == 10000 && !phone.IsSounding(11000), "A running stream is tracked wrongly");
    phone.NoteAudio(12000);   // after a pause: a new start
    Check(phone.StartMs() == 12000 && phone.IsSounding(12100), "A new start of the phone's music was missed");
    // The radio's player started at 11000, the phone at 12000: the phone takes over.
    Check(IsPhoneTakingOver(true, 11000, phone.IsSounding(12100), phone.StartMs()), "The phone's newer music did not take over");
    // The radio's player started after the phone (whose music runs out after its pause key): it keeps the sound.
    Check(!IsPhoneTakingOver(true, 12050, true, 12000), "The phone's old music took the sound back");
    Check(!IsPhoneTakingOver(false, 11000, true, 12000) && !IsPhoneTakingOver(true, 11000, false, 12000), "Nothing to give way to, but it did");
    // The phone's output passes everything on and reports the audio.
    auto inner = std::make_shared<CountingOutput>();
    auto activity = std::make_shared<MediaActivity>();
    WatchedOutput watched(inner, activity);
    watched.Write({});
    Check(!activity->IsSounding(SteadyNowMs()), "Empty audio counted as the phone playing");
    const std::vector<std::uint8_t> pcm(64, 1);
    watched.Write(pcm);
    watched.Flush();
    Check(inner->bytes == 64 && inner->isFlushed && watched.Queued() == 7 && activity->IsSounding(SteadyNowMs()), "The watched output does not pass on or report");
}
}

// The audio buffers, the PCM math, the volume and which source may sound.
void RunAudioTests()
{
    TestRingBuffer();
    TestPcmMath();
    TestAudioState();
    TestAudioFocus();
}
