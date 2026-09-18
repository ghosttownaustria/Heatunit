#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include <cstdint>
#include <thread>
#include <vector>

using namespace headunit;
void Check(bool isValid, const char* message);

namespace {
std::vector<std::uint8_t> Bytes(std::initializer_list<int> values) {
    std::vector<std::uint8_t> bytes;
    for (const int value : values) bytes.push_back(static_cast<std::uint8_t>(value));
    return bytes;
}
std::vector<std::uint8_t> Pcm(std::initializer_list<int> samples) {
    std::vector<std::uint8_t> bytes;
    for (const int sample : samples) {
        const auto value = static_cast<std::int16_t>(sample);
        bytes.push_back(static_cast<std::uint8_t>(value & 0xff));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
    }
    return bytes;
}
std::int16_t SampleAt(const std::vector<std::uint8_t>& bytes, std::size_t index) {
    return static_cast<std::int16_t>(bytes[index * 2] | (bytes[index * 2 + 1] << 8));
}

void TestTouchMapping() {
    // A 1000x400 widget shows the 800x480 video at 666x400, centred with 167 px bars left and right.
    const auto center = MapToTouch(1000, 400, 800, 480, 500, 200, false);
    Check(center && center->first == 400 && center->second == 240, "Centre of the picture is not the centre of the touchscreen");
    Check(!MapToTouch(1000, 400, 800, 480, 100, 200, false), "A press on the black bar became a touch");
    Check(!MapToTouch(1000, 400, 800, 480, 900, 200, false), "A press on the right bar became a touch");
    const auto topLeft = MapToTouch(1000, 400, 800, 480, 167, 0, false);
    Check(topLeft && topLeft->first <= 1 && topLeft->second == 0, "Top-left corner of the picture is wrong");
    const auto bottomRight = MapToTouch(1000, 400, 800, 480, 832, 399, false);
    Check(bottomRight && bottomRight->first >= kTouchWidth - 3 && bottomRight->second >= kTouchHeight - 3, "Bottom-right corner of the picture is wrong");
    // A finger that is already down keeps reporting when it leaves the picture.
    const auto clamped = MapToTouch(1000, 400, 800, 480, 5000, -50, true);
    Check(clamped && clamped->first == kTouchWidth - 1 && clamped->second == 0, "Dragging outside the picture is not clamped to its edge");
    // A tall widget letterboxes the other way.
    const auto tall = MapToTouch(400, 1000, 800, 480, 200, 500, false);
    Check(tall && tall->first == 400 && tall->second == 240, "Vertical letterboxing is wrong");
    Check(!MapToTouch(0, 400, 800, 480, 1, 1, true), "An empty widget produced a touch");
}

void TestProjectionInput() {
    ProjectionInput input;
    input.Tap(keys::Home);
    Check(!input.IsAttached(), "Input reports itself attached without a session");

    std::vector<InputEvent> received;
    const auto token = input.Attach([&](const InputEvent& event) { received.push_back(event); });
    Check(input.IsAttached(), "Input is not attached after Attach");
    input.Touch(TouchAction::Down, 10, 20);
    input.Rotate(2);
    input.Rotate(0);
    input.Tap(keys::Back);
    Check(received.size() == 4, "Events were lost or a zero rotation was sent");
    const auto* touch = std::get_if<TouchInput>(&received[0]);
    Check(touch && touch->action == TouchAction::Down && touch->x == 10 && touch->y == 20, "Touch event changed in transit");
    const auto* rotary = std::get_if<RotaryInput>(&received[1]);
    Check(rotary && rotary->delta == 2, "Rotary event changed in transit");
    const auto* down = std::get_if<KeyInput>(&received[2]);
    const auto* up = std::get_if<KeyInput>(&received[3]);
    Check(down && down->keycode == keys::Back && down->isDown && up && !up->isDown, "A tap is not key down then key up");

    // A finished session must not detach the session that replaced it.
    const auto newToken = input.Attach([&](const InputEvent&) { received.push_back(RotaryInput{99}); });
    input.Detach(token);
    Check(input.IsAttached(), "A stale token detached the newer session");
    input.Detach(newToken);
    Check(!input.IsAttached(), "Detach did not detach");
    const auto count = received.size();
    input.Rotate(1);
    Check(received.size() == count, "An event reached a detached session");

    // Sending from the window thread while the session detaches must not crash or hang.
    std::atomic_bool isRunning{true};
    std::atomic<int> delivered{0};
    std::thread sender([&] { while (isRunning) input.Tap(keys::DpadUp); });
    for (int i = 0; i < 200; ++i) {
        const auto attached = input.Attach([&](const InputEvent&) { ++delivered; });
        std::this_thread::yield();
        input.Detach(attached);
    }
    isRunning = false;
    sender.join();
    Check(!input.IsAttached(), "Racing attach/detach left the input attached");
}

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
}

void TestCarControls() {
    TestTouchMapping();
    TestProjectionInput();
    TestRingBuffer();
    TestPcmMath();
    TestAudioState();
}
