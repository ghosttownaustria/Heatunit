#include "androidauto/ConsoleController.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <utility>
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

bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
using Screen = ConsoleController::Screen;

void TestConsoleController() {
    ConsoleController console;
    // Without a projection there is only the radio, and it has no functions yet: everything is a log line.
    auto effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Radio-Startmenue") && !effect.connect, "Home without a projection did not report the radio menu");
    effect = console.Press(ConsoleKey::Media);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "nicht verbunden"), "Media without a projection was sent or not reported");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && !effect.message.empty(), "Back without a projection was sent or not reported");
    effect = console.Press(ConsoleKey::Projection);
    Check(effect.connect && effect.phoneKeys.empty(), "The projection key did not ask for a connection");

    // Connected: Home goes to the phone's home screen first, the second press to the radio menu.
    console.SetProjectionConnected(true);
    Check(console.CurrentScreen() == Screen::Projection, "A new projection did not start on the phone");
    effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Home} && Contains(effect.message, "Android-Auto-Startbildschirm") &&
        console.CurrentScreen() == Screen::ProjectionHome, "First Home did not go to the phone's home screen");
    effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Home: Radio-Startmenue") && console.CurrentScreen() == Screen::RadioHome,
        "Second Home did not open the radio menu");
    effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Home} && Contains(effect.message, "zurueck") && console.CurrentScreen() == Screen::ProjectionHome,
        "Home in the radio menu did not return to the phone's home screen");

    // Turning and nudging only move the focus; pressing the rotary opens something.
    console.NoteKey(keys::DpadDown, true);
    console.NoteKey(keys::DpadCenter, false);
    Check(console.CurrentScreen() == Screen::ProjectionHome, "Moving the focus left the home screen");
    console.NoteKey(keys::DpadCenter, true);
    Check(console.CurrentScreen() == Screen::Projection, "Pressing the rotary did not leave the home screen");
    // From anywhere else the first Home goes to the phone again, not to the radio.
    Check(console.Press(ConsoleKey::Home).phoneKeys == std::vector<unsigned>{keys::Home}, "Home after leaving the home screen skipped the phone");
    console.NoteTouch();
    Check(console.CurrentScreen() == Screen::Projection, "A touch did not leave the home screen");
    Check(console.Press(ConsoleKey::Home).phoneKeys == std::vector<unsigned>{keys::Home}, "Home after a touch skipped the phone");

    // Launching keys leave the home screen and go to the phone with their own key codes.
    effect = console.Press(ConsoleKey::Media);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Media} && console.CurrentScreen() == Screen::Projection, "Media is wrong");
    Check(console.Press(ConsoleKey::Tel).phoneKeys == std::vector<unsigned>{keys::Tel}, "Tel is wrong");
    Check(console.Press(ConsoleKey::Nav).phoneKeys == std::vector<unsigned>{keys::Navigation}, "Nav is wrong");
    Check(console.Press(ConsoleKey::Map).phoneKeys == std::vector<unsigned>{keys::Navigation}, "Map is wrong");
    Check(console.Press(ConsoleKey::Option).phoneKeys == std::vector<unsigned>{keys::Menu}, "Option is wrong");
    Check(console.Press(ConsoleKey::Home).phoneKeys == std::vector<unsigned>{keys::Home}, "Home after launching skipped the phone");

    // Back is a plain key on the phone and never changes where the phone is.
    Check(console.CurrentScreen() == Screen::ProjectionHome, "Setup for the Back test failed");
    Check(console.Press(ConsoleKey::Back).phoneKeys == std::vector<unsigned>{keys::Back} && console.CurrentScreen() == Screen::ProjectionHome, "Back changed the home state");

    // Radio and Menu have no function yet: only reported, and the radio side is what is "open".
    effect = console.Press(ConsoleKey::Radio);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Radio") && console.CurrentScreen() == Screen::RadioHome, "Radio is wrong");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::Projection, "Back in the radio menu did not return to the phone");
    effect = console.Press(ConsoleKey::Menu);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Menue") && console.CurrentScreen() == Screen::RadioHome, "Menu is wrong");

    // The projection key brings the phone back to the front without sending anything.
    effect = console.Press(ConsoleKey::Projection);
    Check(effect.phoneKeys.empty() && !effect.connect && console.CurrentScreen() == Screen::Projection, "The projection key did not bring the phone forward");

    // The phone goes away: only the radio is left.
    console.SetProjectionConnected(false);
    Check(console.CurrentScreen() == Screen::RadioHome && !console.IsProjectionConnected(), "Disconnecting did not fall back to the radio");
    console.NoteTouch();
    Check(console.CurrentScreen() == Screen::RadioHome, "A touch without a projection changed the screen");
}

// A picture of the phone, dark like Android Auto's bar, with the navigation bar button drawn in one of
// its two symbols. The shapes follow measurements of a real phone: nine 6x5 dots on a 10 px grid, or a
// 31x30 frame with a divider and a bar in its right half.
struct Picture {
    int width, height;
    double scale;
    std::vector<std::uint8_t> rgb;
    explicit Picture(double scaleFactor = 1.0)
        : width(static_cast<int>(kTouchWidth * scaleFactor)), height(static_cast<int>(kTouchHeight * scaleFactor)), scale(scaleFactor),
          rgb(static_cast<std::size_t>(width) * height * 3, 0) {}
    void SetPixel(int x, int y, std::uint8_t value) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        for (int channel = 0; channel < 3; ++channel) rgb[(static_cast<std::size_t>(y) * width + x) * 3 + channel] = value;
    }
    // Rectangle in 800x480 touch coordinates, both corners included.
    void Fill(int x0, int y0, int x1, int y1, std::uint8_t value = 240) {
        const int pixelsPerUnit = static_cast<int>(scale);
        for (int y = static_cast<int>(y0 * scale); y < static_cast<int>(y1 * scale) + pixelsPerUnit; ++y)
            for (int x = static_cast<int>(x0 * scale); x < static_cast<int>(x1 * scale) + pixelsPerUnit; ++x) SetPixel(x, y, value);
    }
    void Dots() {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) Fill(31 + column * 10, 428 + row * 10, 36 + column * 10, 432 + row * 10);
    }
    void Frame() {
        Fill(29, 424, 59, 426);   // top edge
        Fill(29, 451, 59, 453);   // bottom edge
        Fill(29, 424, 31, 453);   // left edge
        Fill(57, 424, 59, 453);   // right edge
        Fill(43, 424, 46, 453);   // divider
        Fill(47, 437, 59, 441);   // bar in the right half
    }
    // A focus ring around the button, in touch coordinates. It reaches into the inspected box only at its corners.
    void Ring() {
        for (int y = 400; y < 480; ++y)
            for (int x = 0; x < 100; ++x) {
                const double distance = std::hypot(x - 44.0, y - 439.0);
                if (distance >= 28.0 && distance <= 31.0) Fill(x, y, x, y);
            }
    }
    PhoneScreen Detect() const { return DetectPhoneScreen(rgb.data(), width, height, width * 3); }
};

void TestPhoneScreenDetector() {
    Picture dashboard;
    dashboard.Dots();
    Check(dashboard.Detect() == PhoneScreen::Dashboard, "Nine dots were not read as the dashboard");
    Picture app;
    app.Frame();
    Check(app.Detect() == PhoneScreen::Other, "The framed symbol was not read as another screen");
    // A focus ring reaches into the box from outside and must not change the reading.
    Picture ringFrame;
    ringFrame.Frame();
    ringFrame.Ring();
    Check(ringFrame.Detect() == PhoneScreen::Other, "A focus ring around the frame changed the reading");
    Picture ringDots;
    ringDots.Dots();
    ringDots.Ring();
    Check(ringDots.Detect() == PhoneScreen::Dashboard, "A focus ring around the dots changed the reading");
    // Only the ring, without a symbol, tells nothing.
    Picture ringOnly;
    ringOnly.Ring();
    Check(ringOnly.Detect() == PhoneScreen::Unknown, "A focus ring alone was read as a known screen");
    // Other picture sizes carry the same layout.
    Picture bigDots(2.0);
    bigDots.Dots();
    Check(bigDots.Detect() == PhoneScreen::Dashboard, "Dots in a 1600x960 picture were misread");
    Picture bigFrame(2.0);
    bigFrame.Frame();
    Check(bigFrame.Detect() == PhoneScreen::Other, "The frame in a 1600x960 picture was misread");
    // Anything else is unknown: a black picture (splash screen, transition) and a light, flooded bar.
    Check(Picture().Detect() == PhoneScreen::Unknown, "A black picture was read as a known screen");
    Picture light;
    light.Fill(0, 400, 799, 479);
    Check(light.Detect() == PhoneScreen::Unknown, "A light bar was read as a known screen");
    Picture noise;
    noise.Fill(35, 430, 37, 432);
    noise.Fill(45, 430, 47, 432);
    Check(noise.Detect() == PhoneScreen::Unknown, "Two stray blobs were read as a known screen");
    Check(DetectPhoneScreen(nullptr, 800, 480, 2400) == PhoneScreen::Unknown, "A missing picture was read as a known screen");
    Check(DetectPhoneScreen(dashboard.rgb.data(), 100, 60, 300) == PhoneScreen::Unknown, "A tiny picture was read as a known screen");
}

// Home reads where the phone really is from its picture instead of guessing from what was sent.
void TestConsoleHomeWithPicture() {
    using Taps = std::vector<std::pair<int, int>>;
    const Taps dashboardTap{{kDashboardButtonX, kDashboardButtonY}};
    ConsoleController console;
    console.SetProjectionConnected(true);
    // The phone shows an app: Home taps the dashboard button (the phone's home key only opens its launcher).
    auto effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneKeys.empty() && effect.phoneTaps == dashboardTap && !effect.retryDashboard && Contains(effect.message, "Android-Auto-Startbildschirm") &&
        console.CurrentScreen() == Screen::ProjectionHome, "Home from an app did not tap the dashboard button");
    // The phone shows its dashboard: Home is the second step and leaves the phone alone.
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && Contains(effect.message, "Home: Radio-Startmenue") && console.CurrentScreen() == Screen::RadioHome,
        "Home on the dashboard did not open the radio menu");
    // Back from the radio menu: the phone still shows its dashboard, so nothing is sent.
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && !effect.retryDashboard && Contains(effect.message, "zurueck") &&
        console.CurrentScreen() == Screen::ProjectionHome, "Home in the radio menu sent something to a phone that shows its dashboard");
    // The picture wins over the controller's own idea: it says ProjectionHome, the phone shows an app.
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneTaps == dashboardTap && console.CurrentScreen() == Screen::ProjectionHome && !Contains(effect.message, "Radio"),
        "Home trusted its own state over the picture");
    // In the radio menu with the phone on an app: back to the phone means its dashboard.
    console.Press(ConsoleKey::Radio);
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneTaps == dashboardTap && Contains(effect.message, "zurueck") && console.CurrentScreen() == Screen::ProjectionHome,
        "Home from the radio menu did not bring an app back to the dashboard");
    // The picture cannot be read: the phone's own home key, then a second look at the picture.
    console.SetProjectionConnected(true);
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Unknown);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Home} && effect.phoneTaps.empty() && effect.retryDashboard && console.CurrentScreen() == Screen::ProjectionHome,
        "Home with an unreadable picture did not fall back to the home key and a second look");
    // Without a projection the picture is irrelevant.
    console.SetProjectionConnected(false);
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && Contains(effect.message, "Radio-Startmenue"), "Home without a projection used the picture");
    // Only Home looks at the picture.
    console.SetProjectionConnected(true);
    effect = console.Press(ConsoleKey::Media, PhoneScreen::Dashboard);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Media} && effect.phoneTaps.empty() && !effect.retryDashboard, "Media depended on the picture");
}
}

void TestCarControls() {
    TestTouchMapping();
    TestProjectionInput();
    TestRingBuffer();
    TestPcmMath();
    TestAudioState();
    TestConsoleController();
    TestPhoneScreenDetector();
    TestConsoleHomeWithPicture();
}
