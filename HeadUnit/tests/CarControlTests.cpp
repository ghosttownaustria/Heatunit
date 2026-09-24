#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include "media/MusicLibrary.h"
#include "media/StreamText.h"
#include "ui/HomeMenuLayout.h"
#include "ui/KnobZones.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
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
    Check(bottomRight && bottomRight->first >= kDefaultDisplay.width - 3 && bottomRight->second >= kDefaultDisplay.height - 3, "Bottom-right corner of the picture is wrong");
    // A finger that is already down keeps reporting when it leaves the picture.
    const auto clamped = MapToTouch(1000, 400, 800, 480, 5000, -50, true);
    Check(clamped && clamped->first == kDefaultDisplay.width - 1 && clamped->second == 0, "Dragging outside the picture is not clamped to its edge");
    // A tall widget letterboxes the other way.
    const auto tall = MapToTouch(400, 1000, 800, 480, 200, 500, false);
    Check(tall && tall->first == 400 && tall->second == 240, "Vertical letterboxing is wrong");
    Check(!MapToTouch(0, 400, 800, 480, 1, 1, true), "An empty widget produced a touch");
}

// The touchscreen has the size of the chosen display, not always 800x480.
void TestTouchMappingOtherDisplays() {
    const DisplayConfig hd{1280, 720}, fullHd{1920, 1080};
    const auto center = MapToTouch(1280, 720, 1280, 720, 640, 360, false, hd);
    Check(center && center->first == 640 && center->second == 360, "Centre of a 1280x720 picture is not the centre of its touchscreen");
    // A widget of half the size: the far corner is the last touch position.
    const auto corner = MapToTouch(640, 360, 1280, 720, 639.5, 359.5, false, hd);
    Check(corner && corner->first >= 1278 && corner->first <= 1279 && corner->second >= 718 && corner->second <= 719, "Bottom-right corner of a 1280x720 picture is wrong");
    const auto fullCorner = MapToTouch(960, 540, 1920, 1080, 959, 539, false, fullHd);
    Check(fullCorner && fullCorner->first >= 1917 && fullCorner->first <= 1919 && fullCorner->second >= 1077 && fullCorner->second <= 1079, "Bottom-right corner of a 1920x1080 picture is wrong");
    // A wide widget letterboxes the 16:9 picture with bars left and right.
    const auto letterboxed = MapToTouch(1000, 400, 1280, 720, 500, 200, false, hd);
    Check(letterboxed && std::abs(letterboxed->first - 640) <= 1 && std::abs(letterboxed->second - 360) <= 1, "Letterboxing a 1280x720 picture is wrong");
    Check(!MapToTouch(1000, 400, 1280, 720, 100, 200, false, hd), "A press on the black bar became a touch on a 1280x720 display");
    const auto clamped = MapToTouch(1000, 400, 1280, 720, 5000, 5000, true, hd);
    Check(clamped && clamped->first == 1279 && clamped->second == 719, "Dragging outside a 1280x720 picture is not clamped to its edge");
    // Without a display the default size is used.
    const auto standard = MapToTouch(800, 480, 800, 480, 799, 479, false);
    Check(standard && standard->first == 799 && standard->second == 479, "The default touchscreen is not 800x480");
    // The 1600x600 display rides in a 1920x1080 frame; its touchscreen is the shown 1920x720 area, not the frame.
    const DisplayConfig ultrawide{1600, 600};
    const auto wideCenter = MapToTouch(1920, 720, 1920, 720, 960, 360, false, ultrawide);
    Check(wideCenter && wideCenter->first == 960 && wideCenter->second == 360, "Centre of the 1600x600 picture is not the centre of its touchscreen");
    const auto wideCorner = MapToTouch(960, 360, 1920, 720, 959.5, 359.5, false, ultrawide);
    Check(wideCorner && wideCorner->first >= 1918 && wideCorner->first <= 1919 && wideCorner->second >= 718 && wideCorner->second <= 719, "Bottom-right corner of the 1600x600 picture is wrong");
    const auto wideLetterboxed = MapToTouch(1000, 1000, 1920, 720, 500, 500, false, ultrawide);   // shown 1000x375, 312 px bars above and below
    Check(wideLetterboxed && std::abs(wideLetterboxed->first - 960) <= 1 && std::abs(wideLetterboxed->second - 360) <= 1, "Letterboxing the 1600x600 picture is wrong");
    Check(!MapToTouch(1000, 1000, 1920, 720, 500, 100, false, ultrawide), "A press on the bar above the 1600x600 picture became a touch");
    const auto wideClamped = MapToTouch(1000, 1000, 1920, 720, 5000, 5000, true, ultrawide);
    Check(wideClamped && wideClamped->first == 1919 && wideClamped->second == 719, "Dragging outside the 1600x600 picture is not clamped to its edge");
}

void TestDisplayConfig() {
    Check(std::size(kDisplays) == 4 && kDisplays[0] == DisplayConfig{800, 480} && kDisplays[1] == DisplayConfig{1280, 720} && kDisplays[2] == DisplayConfig{1600, 600} &&
        kDisplays[3] == DisplayConfig{1920, 1080}, "The offered display sizes changed");
    Check(kDefaultDisplay == DisplayConfig{800, 480}, "The default display is not 800x480");
    Check(DisplayDensity({800, 480}) == 160 && DisplayDensity({1280, 720}) == 240 && DisplayDensity({1920, 1080}) == 360, "The density does not follow the display height");
    Check(DisplayDensity({1600, 600}) == 240, "The density of the 1600x600 display does not follow its shown height (720)");
    for (const auto& display : kDisplays) Check(IsSupportedDisplay(display), "An offered display is not supported");
    Check(!IsSupportedDisplay({1024, 600}) && !IsSupportedDisplay({480, 800}) && !IsSupportedDisplay({0, 0}) && !IsSupportedDisplay({1280, 480}), "An unknown display was accepted");
    Check(DisplayText({1280, 720}) == "1280 x 720", "The display text is wrong");
    for (const char* text : {"1280x720", "1280 x 720", "1280X720", "  1280 x 720 ", "1280 X 720"})
        Check(ParseDisplay(text) == std::optional<DisplayConfig>(DisplayConfig{1280, 720}), "A valid display text was not parsed");
    Check(ParseDisplay("800x480") == std::optional<DisplayConfig>(DisplayConfig{800, 480}) && ParseDisplay("1920x1080") == std::optional<DisplayConfig>(DisplayConfig{1920, 1080}) &&
        ParseDisplay("1600x600") == std::optional<DisplayConfig>(DisplayConfig{1600, 600}) && ParseDisplay("1600 X 600") == std::optional<DisplayConfig>(DisplayConfig{1600, 600}),
        "The other display texts were not parsed");
    // How a display is carried by the frame: the smallest fixed resolution that holds it, the display fitted
    // inside with margins (sum of both sides, even), the shown area is what the head unit shows and touches.
    Check(VideoLayoutOf({800, 480}) == VideoLayout{800, 480, 0, 0, 800, 480}, "800x480 is not carried by a 800x480 frame");
    Check(VideoLayoutOf({1280, 720}) == VideoLayout{1280, 720, 0, 0, 1280, 720}, "1280x720 is not carried by a 1280x720 frame");
    Check(VideoLayoutOf({1920, 1080}) == VideoLayout{1920, 1080, 0, 0, 1920, 1080}, "1920x1080 is not carried by a 1920x1080 frame");
    const auto ultra = VideoLayoutOf({1600, 600});
    Check(ultra == VideoLayout{1920, 1080, 0, 360, 1920, 720}, "1600x600 is not fitted into the 1920x1080 frame with a 360 px height margin");
    Check(ultra.HasMargins() && ultra.Left() == 0 && ultra.Top() == 180 && !VideoLayoutOf({1280, 720}).HasMargins(), "The shown area of the 1600x600 display starts in the wrong place");
    Check(VideoLayoutOf({1024, 600}) == VideoLayout{1280, 720, 52, 0, 1228, 720}, "A display taller than its frame is not fitted with a width margin");
    Check(VideoLayoutOf({3840, 2160}) == VideoLayout{0, 0, 0, 0, 3840, 2160} && VideoLayoutOf({1920, 1200}) == VideoLayout{0, 0, 0, 0, 1920, 1200},
        "A display that no frame holds got a frame");
    Check(VideoLayoutOf({0, 0}).codecWidth == 0 && VideoLayoutOf({-800, 480}).codecWidth == 0 && VideoLayoutOf({800, 0}).codecWidth == 0, "A display without a size got a frame");
    for (const auto& display : kDisplays) {
        const auto layout = VideoLayoutOf(display);
        Check(layout.codecWidth > 0 && layout.marginWidth % 2 == 0 && layout.marginHeight % 2 == 0 && layout.width == layout.codecWidth - layout.marginWidth &&
            layout.height == layout.codecHeight - layout.marginHeight, ("The layout of " + DisplayText(display) + " is inconsistent").c_str());
        Check(layout.width * display.height == layout.height * display.width, ("The shown area of " + DisplayText(display) + " has another shape than the display").c_str());
    }
    for (const char* text : {"", "x", "1280", "1280x", "x720", "1024x600", "1280x720x1", "abc", "1280x720p", "-1280x720", "+1280x720", "1280,720", "720x1280", "0x0"})
        Check(!ParseDisplay(text), "An invalid display text was accepted");
    for (const auto& display : kDisplays) Check(ParseDisplay(DisplayText(display)) == std::optional<DisplayConfig>(display), "A display text does not read back");
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
    // Media without a phone: the radio's own music player; Back and Home lead from it to the home menu.
    effect = console.Press(ConsoleKey::Media);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::Multimedia && !effect.message.empty(), "Media without a projection did not open the music player");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::RadioHome, "Back on the music player did not return to the home menu");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && !effect.message.empty() && console.CurrentScreen() == Screen::RadioHome, "Back without a projection was sent or not reported");
    console.Press(ConsoleKey::Radio);
    Check(console.CurrentScreen() == Screen::Radio, "Radio did not open the tuner");
    effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::RadioHome, "Home on the tuner did not return to the home menu");
    // The tiles' pages.
    effect = console.Open(Screen::Multimedia);
    Check(console.CurrentScreen() == Screen::Multimedia && !effect.message.empty(), "The Multimedia tile did not open the music player");
    effect = console.Open(Screen::Projection);
    Check(console.CurrentScreen() == Screen::Multimedia && effect.message.empty(), "Opening the phone as a page changed the screen");
    console.Press(ConsoleKey::Menu);
    Check(ConsoleController::IsRadioScreen(Screen::RadioHome) && ConsoleController::IsRadioScreen(Screen::Radio) &&
        !ConsoleController::IsRadioScreen(Screen::ProjectionHome) && ConsoleController::IsRadioPage(Screen::Multimedia) &&
        !ConsoleController::IsRadioPage(Screen::RadioHome), "The radio's screens are not told apart");
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

    // Radio opens the tuner, Menu the home menu; nothing goes to the phone. Back goes from the tuner to the home menu and
    // from there to the phone.
    effect = console.Press(ConsoleKey::Radio);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Radio") && console.CurrentScreen() == Screen::Radio, "Radio is wrong");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::RadioHome, "Back on the tuner did not return to the home menu");
    effect = console.Press(ConsoleKey::Back);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::Projection, "Back in the radio menu did not return to the phone");
    // With a phone, Media is the phone's media app, even from the radio's music player.
    console.Open(Screen::Multimedia);
    Check(console.Press(ConsoleKey::Media).phoneKeys == std::vector<unsigned>{keys::Media} && console.CurrentScreen() == Screen::Projection,
        "Media with a projection did not go to the phone");
    console.Open(Screen::Radio);
    effect = console.Press(ConsoleKey::Projection);
    Check(effect.phoneKeys.empty() && console.CurrentScreen() == Screen::Projection, "The projection key did not leave the tuner");
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
// 31x30 frame with a divider and a bar in its right half. Drawn in the 800x480 layout and scaled by the
// display height, like the phone scales its interface (see LayoutScale).
struct Picture {
    int width, height;
    double scale;
    std::vector<std::uint8_t> rgb;
    // The picture the head unit gets to see: the shown area of the display (for 1600x600 that is 1920x720).
    explicit Picture(const DisplayConfig& display = kDefaultDisplay)
        : width(VideoLayoutOf(display).width), height(VideoLayoutOf(display).height), scale(LayoutScale(VideoLayoutOf(display).height)),
          rgb(static_cast<std::size_t>(width) * height * 3, 0) {}
    void SetPixel(int x, int y, std::uint8_t value) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        for (int channel = 0; channel < 3; ++channel) rgb[(static_cast<std::size_t>(y) * width + x) * 3 + channel] = value;
    }
    // Rectangle in the 800x480 layout, both corners included.
    void Fill(int x0, int y0, int x1, int y1, std::uint8_t value = 240) {
        for (int y = static_cast<int>(y0 * scale); y < static_cast<int>((y1 + 1) * scale); ++y)
            for (int x = static_cast<int>(x0 * scale); x < static_cast<int>((x1 + 1) * scale); ++x) SetPixel(x, y, value);
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
    // A focus ring around the button (radius 28 to 31 layout units). It reaches into the inspected box only at its corners.
    void Ring() {
        for (int y = static_cast<int>(400 * scale); y < height; ++y)
            for (int x = 0; x < static_cast<int>(100 * scale); ++x) {
                const double distance = std::hypot(x / scale - 44.0, y / scale - 439.0);
                if (distance >= 28.0 && distance <= 31.0) SetPixel(x, y, 240);
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
    // Every display size carries the same layout, scaled by its height (the wide ones are only wider).
    for (const auto& display : kDisplays) {
        const std::string size = DisplayText(display);
        Picture dots(display);
        dots.Dots();
        Check(dots.Detect() == PhoneScreen::Dashboard, ("Dots in a " + size + " picture were misread").c_str());
        Picture frame(display);
        frame.Frame();
        Check(frame.Detect() == PhoneScreen::Other, ("The frame in a " + size + " picture was misread").c_str());
        Picture ringedFrame(display);
        ringedFrame.Frame();
        ringedFrame.Ring();
        Check(ringedFrame.Detect() == PhoneScreen::Other, ("A focus ring changed the reading of a " + size + " picture with the frame").c_str());
        Picture ringedDots(display);
        ringedDots.Dots();
        ringedDots.Ring();
        Check(ringedDots.Detect() == PhoneScreen::Dashboard, ("A focus ring changed the reading of a " + size + " picture with the dots").c_str());
        Check(Picture(display).Detect() == PhoneScreen::Unknown, ("A black " + size + " picture was read as a known screen").c_str());
        Picture ringAlone(display);
        ringAlone.Ring();
        Check(ringAlone.Detect() == PhoneScreen::Unknown, ("A focus ring alone was read as a known screen in a " + size + " picture").c_str());
    }
    // A 800x480 layout drawn into a wider picture without the scaling would not be found: the layout follows the height.
    Picture unscaled(DisplayConfig{1280, 720});
    unscaled.scale = 1.0;
    unscaled.Dots();
    Check(unscaled.Detect() != PhoneScreen::Dashboard, "Dots at the 800x480 position were read in a 1280x720 picture");
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
    console.Press(ConsoleKey::Menu);
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

// The dashboard button sits at the same place of the phone's layout at every display size.
void TestDashboardButtonOnDisplays() {
    Check(DashboardButtonPosition(kDefaultDisplay) == std::make_pair(42, 438), "The dashboard button moved on the default display");
    Check(DashboardButtonPosition({1280, 720}) == std::make_pair(63, 657), "The dashboard button is wrong on a 1280x720 display");
    // The 1600x600 display is shown as 1920x720: the button is where it is on a 720 px high display.
    Check(DashboardButtonPosition({1600, 600}) == std::make_pair(63, 657), "The dashboard button is wrong on a 1600x600 display");
    for (const auto& display : kDisplays) {
        const auto [x, y] = DashboardButtonPosition(display);
        const auto layout = VideoLayoutOf(display);
        const double scale = LayoutScale(layout.height);
        // Inside the button's symbol (x 29..59, y 424..453 of the layout) and on the shown area.
        Check(x >= 29 * scale && x <= 60 * scale && y >= 424 * scale && y <= 454 * scale && x < layout.width && y < layout.height,
            ("The dashboard button is off its symbol on a " + DisplayText(display) + " display").c_str());
    }
    // Home taps that spot, whatever display the console was told about.
    using Taps = std::vector<std::pair<int, int>>;
    ConsoleController console;
    console.SetDisplay({1280, 720});
    console.SetProjectionConnected(true);
    Check(console.Display() == DisplayConfig{1280, 720}, "The console forgot its display");
    auto effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneTaps == Taps{{63, 657}} && effect.phoneKeys.empty(), "Home tapped the wrong spot on a 1280x720 display");
    console.SetDisplay({1920, 1080});
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);   // radio menu
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);       // back to the phone, phone shows an app
    Check(effect.phoneTaps == Taps{DashboardButtonPosition({1920, 1080})}, "Home tapped the wrong spot on a 1920x1080 display");
    console.SetDisplay({1600, 600});
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);   // radio menu
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);       // back to the phone, phone shows an app
    Check(effect.phoneTaps == Taps{{63, 657}}, "Home tapped the wrong spot on a 1600x600 display");
    console.SetDisplay({1920, 1080});
    // A new connection keeps the display.
    console.SetProjectionConnected(false);
    console.SetProjectionConnected(true);
    Check(console.Display() == DisplayConfig{1920, 1080}, "Connecting changed the console's display");
}
}

// The round controller: four arrows on its rim, a push button in the middle, nothing outside.
void TestKnobZones() {
    constexpr double radius = 100;
    Check(KnobZoneAt(0, 0, radius) == KnobZone::Centre, "The middle of the controller is not its push button");
    Check(KnobZoneAt(0, -80, radius) == KnobZone::Up && KnobZoneAt(80, 0, radius) == KnobZone::Right &&
        KnobZoneAt(0, 80, radius) == KnobZone::Down && KnobZoneAt(-80, 0, radius) == KnobZone::Left, "The arrows are not where they are drawn");
    // The push button ends at half the radius.
    Check(KnobZoneAt(0, -49, radius) == KnobZone::Centre && KnobZoneAt(0, -51, radius) == KnobZone::Up &&
        KnobZoneAt(-49, 0, radius) == KnobZone::Centre && KnobZoneAt(-51, 0, radius) == KnobZone::Left, "The push button has the wrong size");
    // The arrows share the rim in four 90 degree sectors: the larger distance from the middle wins.
    Check(KnobZoneAt(60, -50, radius) == KnobZone::Right && KnobZoneAt(50, -60, radius) == KnobZone::Up &&
        KnobZoneAt(50, 60, radius) == KnobZone::Down && KnobZoneAt(60, 50, radius) == KnobZone::Right &&
        KnobZoneAt(-50, 60, radius) == KnobZone::Down && KnobZoneAt(-60, 50, radius) == KnobZone::Left &&
        KnobZoneAt(-60, -50, radius) == KnobZone::Left && KnobZoneAt(-50, -60, radius) == KnobZone::Up, "The sectors of the arrows are wrong");
    // Rim included, everything beyond it is no zone; so is a controller without a size.
    Check(KnobZoneAt(0, -100, radius) == KnobZone::Up && KnobZoneAt(0, -101, radius) == KnobZone::None &&
        KnobZoneAt(75, 75, radius) == KnobZone::None && KnobZoneAt(1, 1, 0) == KnobZone::None && KnobZoneAt(1, 1, -5) == KnobZone::None,
        "A click outside the controller hit a zone");
    // All the way round: an arrow with a key on the rim, the push button (no key) inside.
    for (int degrees = 0; degrees < 360; degrees += 5) {
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        const KnobZone rim = KnobZoneAt(std::sin(radians) * 80, -std::cos(radians) * 80, radius);
        const KnobZone inside = KnobZoneAt(std::sin(radians) * 30, -std::cos(radians) * 30, radius);
        Check(rim != KnobZone::None && rim != KnobZone::Centre && KnobZoneKey(rim) != 0, "A point on the rim is not an arrow");
        Check(inside == KnobZone::Centre && KnobZoneKey(inside) == 0, "A point near the middle is not the push button");
    }
    Check(KnobZoneKey(KnobZone::Up) == keys::DpadUp && KnobZoneKey(KnobZone::Right) == keys::DpadRight &&
        KnobZoneKey(KnobZone::Down) == keys::DpadDown && KnobZoneKey(KnobZone::Left) == keys::DpadLeft, "An arrow sends the wrong key");
    Check(KnobZoneKey(KnobZone::Centre) == 0 && KnobZoneKey(KnobZone::None) == 0, "The middle or nothing sends an arrow key");
}

// The radio's home menu: six tiles in one row, laid out as in the 1600x600 design and scrolled to the focus.
void TestHomeMenuLayout() {
    Check(kHomeMenuCount == 6 && std::string(HomeMenuTitle(kHomeMenuEntries[0])) == "Multimedia" &&
        std::string(HomeMenuTitle(kHomeMenuEntries[5])) == "Settings", "The home menu does not have the tiles of the design");
    // A tile does what its controller key does; Vehicle and Settings have nothing behind them yet.
    Check(HomeMenuPage(HomeMenuEntry::Multimedia) == Screen::Multimedia && HomeMenuPage(HomeMenuEntry::Radio) == Screen::Radio &&
        !HomeMenuKey(HomeMenuEntry::Multimedia) && !HomeMenuKey(HomeMenuEntry::Radio) &&
        HomeMenuKey(HomeMenuEntry::Telephone) == ConsoleKey::Tel && HomeMenuKey(HomeMenuEntry::Navigation) == ConsoleKey::Nav &&
        !HomeMenuPage(HomeMenuEntry::Telephone) && !HomeMenuPage(HomeMenuEntry::Navigation) &&
        !HomeMenuKey(HomeMenuEntry::Vehicle) && !HomeMenuKey(HomeMenuEntry::Settings) && !HomeMenuPage(HomeMenuEntry::Vehicle) &&
        !HomeMenuPage(HomeMenuEntry::Settings), "A home menu tile opens the wrong thing");
    // The screen is 600 units high and as wide as the display's shape.
    Check(HomeMenuWidth({1600, 600}) == 1600 && HomeMenuWidth(kDefaultDisplay) == 1000 && std::abs(HomeMenuWidth({1920, 1080}) - 1066.67) < 0.01 &&
        HomeMenuWidth({0, 0}) == 0, "The home menu has the wrong width");
    Check(HomeTileLeft(0) == 25 && HomeTileLeft(4) == 1225 && kHomeRowWidth == 1800, "The tiles are not where the design has them");
    // 1600x600 as in the design: up to Vehicle nothing scrolls (Settings is cut off at the edge), Settings scrolls
    // the row to its end, and going back only scrolls again once a tile would leave the screen.
    for (int focus = 0; focus <= 4; ++focus) Check(HomeMenuScroll(focus, 1600, 0) == 0, "The design's first screen scrolled");
    Check(HomeMenuScroll(5, 1600, 0) == 200, "Settings did not scroll into view");
    Check(HomeMenuScroll(4, 1600, 200) == 200 && HomeMenuScroll(1, 1600, 200) == 200 && HomeMenuScroll(0, 1600, 200) == 0,
        "Going back scrolled more than needed");
    // 800x480 shows three tiles and a bit.
    Check(HomeMenuScroll(2, 1000, 0) == 0 && HomeMenuScroll(3, 1000, 0) == 200 && HomeMenuScroll(5, 1000, 0) == 800,
        "The row scrolls wrongly on 800x480");
    // On every display each tile is completely in view once it has the focus, walking right and back again.
    for (const auto& display : kDisplays) {
        const double width = HomeMenuWidth(display);
        double scroll = 0;
        for (const int focus : {0, 1, 2, 3, 4, 5, 4, 3, 2, 1, 0}) {
            scroll = HomeMenuScroll(focus, width, scroll);
            Check(HomeTileLeft(focus) - scroll >= 0 && HomeTileLeft(focus) + kHomeTileWidth - scroll <= width && scroll >= 0 &&
                scroll <= kHomeRowWidth - width, ("A focused tile is not in view on a " + DisplayText(display) + " display").c_str());
        }
        Check(scroll == 0, "Walking back did not return to the start of the row");
    }
    // A screen wider than the row never scrolls.
    Check(HomeMenuScroll(5, 2400, 0) == 0, "A row that fits scrolled");
    // Clicks: on a tile, in the gaps, above and below the row, with the row scrolled.
    Check(HomeTileAt(150, 300, 0) == 0 && HomeTileAt(1350, 300, 0) == 4 && HomeTileAt(1560, 300, 0) == 5, "A click missed its tile");
    Check(!HomeTileAt(300, 300, 0) && !HomeTileAt(10, 300, 0) && !HomeTileAt(150, 50, 0) && !HomeTileAt(150, 520, 0) && !HomeTileAt(1790, 300, 0),
        "A click outside the tiles hit one");
    Check(HomeTileAt(150, 300, 200) == 1 && HomeTileAt(1560, 300, 200) == 5, "A click on the scrolled row hit the wrong tile");
    // The focus stops at both ends.
    Check(MoveHomeFocus(0, -1) == 0 && MoveHomeFocus(0, 2) == 2 && MoveHomeFocus(4, 3) == 5 && MoveHomeFocus(5, -9) == 0, "The focus left the row");
}

// The player pages: a list that scrolls with the focus, and the focus moving between the top buttons, the controls and
// the list.
void TestPlayerPageFocus() {
    Check(ListFirstRow(0, 0, 5, 20) == 0 && ListFirstRow(4, 0, 5, 20) == 0 && ListFirstRow(5, 0, 5, 20) == 1 && ListFirstRow(19, 0, 5, 20) == 15 &&
        ListFirstRow(10, 15, 5, 20) == 10 && ListFirstRow(12, 10, 5, 20) == 10, "The list does not follow the focus");
    Check(ListFirstRow(3, 0, 5, 3) == 0 && ListFirstRow(2, 7, 5, 3) == 0 && ListFirstRow(0, 4, 5, 0) == 0 && ListFirstRow(1, 0, 0, 5) == 0,
        "A short or empty list scrolled");
    using Part = PageFocus::Part;
    const PageShape shape{2, 3, 10};
    PageFocus focus;   // starts in the list
    focus = TurnFocus(focus, shape, 3);
    Check(focus.part == Part::List && focus.row == 3, "Turning did not move through the list");
    focus = TurnFocus(focus, shape, 50);
    Check(focus.row == 9, "Turning left the end of the list");
    focus = NudgeFocus(focus, shape, keys::DpadLeft);
    Check(focus.part == Part::List && focus.row == 9, "Left moved in the list");
    for (int i = 0; i < 9; ++i) focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::List && focus.row == 0, "Up did not go through the list");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Controls && focus.button == 1 && focus.row == 0, "Up from the list did not reach the play button");
    focus = TurnFocus(focus, shape, -5);
    Check(focus.part == Part::Controls && focus.button == 0, "Turning left the controls");
    focus = NudgeFocus(focus, shape, keys::DpadRight);
    focus = NudgeFocus(focus, shape, keys::DpadRight);
    focus = NudgeFocus(focus, shape, keys::DpadRight);
    Check(focus.part == Part::Controls && focus.button == 2, "Right did not stop at the last control");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Header && focus.button == 0, "Up from the controls did not reach the top buttons");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Header, "Up left the top of the page");
    focus = NudgeFocus(NudgeFocus(focus, shape, keys::DpadDown), shape, keys::DpadDown);
    Check(focus.part == Part::List && focus.row == 0, "Down did not come back to where the list was");
    // The list becomes empty (a new country loads): the focus waits on the play button; a shorter list keeps it in range.
    focus.row = 8;
    Check(FitFocus(focus, {2, 3, 0}) == PageFocus{Part::Controls, 1, 0}, "An empty list kept the focus");
    Check(FitFocus(focus, {2, 3, 4}).row == 3, "A shorter list left the focus beyond its end");
    Check(NudgeFocus(PageFocus{Part::Controls, 1, 0}, {0, 3, 4}, keys::DpadUp).part == Part::Controls, "Up went to top buttons that are not there");
    Check(NudgeFocus(PageFocus{Part::Controls, 1, 0}, {1, 3, 0}, keys::DpadDown).part == Part::Controls, "Down went into an empty list");
}

// The music folder: every playable file below it, in the order people number them, top folder first.
void TestMusicLibrary() {
    Check(NaturalLess("2 Song", "10 Song") && !NaturalLess("10 Song", "2 Song") && NaturalLess("track02", "Track10") &&
        NaturalLess("abc", "ABD") && NaturalLess("Song", "Song 2") && !NaturalLess("same", "same") && !NaturalLess("01 a", "1 a") && !NaturalLess("1 a", "01 a"),
        "Natural order is wrong");
    Check(IsMusicFile("a/b/Song.MP3") && IsMusicFile("x.flac") && IsMusicFile("x.m4a") && IsMusicFile("x.opus") && !IsMusicFile("cover.jpg") &&
        !IsMusicFile("list.m3u") && !IsMusicFile("mp3") && !IsMusicFile("folder.mp3/"), "Music files are not recognised");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "headunit-music-test";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "Album B");
    fs::create_directories(root / "Album A" / "CD 2");
    for (const char* name : {"10 Ten.mp3", "2 Two.flac", "cover.jpg", "Album B/01 First.ogg", "Album A/3 Third.m4a", "Album A/CD 2/1 Disc two.mp3"})
        std::ofstream(root / fs::path(reinterpret_cast<const char8_t*>(name))) << "x";
    fs::create_directories(root / u8"Mötley Crüe");
    std::ofstream(root / u8"Mötley Crüe" / u8"Kickstart ü.mp3") << "x";
    const auto tracks = ScanMusicFolder(root);
    std::vector<std::string> names;
    for (const auto& track : tracks) names.push_back(track.folder + "|" + track.name);
    fs::remove_all(root, ignored);
    const std::vector<std::string> expected{"|2 Two", "|10 Ten", "Album A|3 Third", "Album A/CD 2|1 Disc two", "Album B|01 First",
        "M\xC3\xB6tley Cr\xC3\xBC" "e|Kickstart \xC3\xBC"};
    std::string found;
    for (const auto& name : names) found += " [" + name + "]";
    Check(names == expected, ("The music folder is read in the wrong order or incompletely:" + found).c_str());
    Check(tracks.size() == expected.size() && tracks[1].path.find("10 Ten.mp3") != std::string::npos, "A track has the wrong path");
    Check(ScanMusicFolder(root).empty(), "A missing music folder gave tracks");
}

// "Now playing" of a radio station and the play times.
void TestStreamText() {
    Check(IcyStreamTitle("StreamTitle='Queen - Bohemian Rhapsody';StreamUrl='';") == "Queen - Bohemian Rhapsody", "The ICY title was not read");
    Check(IcyStreamTitle("StreamTitle='Guns N' Roses - Paradise City';") == "Guns N' Roses - Paradise City", "An apostrophe ended the title");
    Check(IcyStreamTitle("StreamTitle='  Spaced  ';") == "Spaced" && IcyStreamTitle("StreamTitle=' - ';").empty() && IcyStreamTitle("StreamTitle='';").empty() &&
        IcyStreamTitle("StreamUrl='x';").empty() && IcyStreamTitle("").empty(), "Empty or odd titles are wrong");
    Check(IcyStreamTitle("StreamTitle='Caf\xE9';") == "Caf\xC3\xA9" && IcyStreamTitle("StreamTitle='Caf\xC3\xA9';") == "Caf\xC3\xA9",
        "A Latin-1 title was not converted, or a UTF-8 one was");
    Check(IsValidUtf8("abc \xC3\xA4\xE2\x82\xAC\xF0\x9F\x8E\xB5") && !IsValidUtf8("\xC3") && !IsValidUtf8("\xE9t\xE9") && !IsValidUtf8("\xC3\x28"), "UTF-8 check is wrong");
    Check(FormatPlayTime(0) == "0:00" && FormatPlayTime(7.9) == "0:07" && FormatPlayTime(187.4) == "3:07" && FormatPlayTime(3725) == "1:02:05" &&
        FormatPlayTime(-3) == "0:00" && FormatPlayTime(std::nan("")) == "0:00", "Play times are formatted wrongly");
}

void TestCarControls() {
    TestKnobZones();
    TestHomeMenuLayout();
    TestPlayerPageFocus();
    TestMusicLibrary();
    TestStreamText();
    TestTouchMapping();
    TestTouchMappingOtherDisplays();
    TestDisplayConfig();
    TestProjectionInput();
    TestRingBuffer();
    TestPcmMath();
    TestAudioState();
    TestConsoleController();
    TestPhoneScreenDetector();
    TestConsoleHomeWithPicture();
    TestDashboardButtonOnDisplays();
}
