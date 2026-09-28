#include "ui/ScriptedPhoneTest.h"
#include "androidauto/ProjectionKeys.h"
#include "platform/Environment.h"
#include <charconv>
#include <cstdlib>
#include <string_view>
#include <system_error>
#include <utility>

namespace headunit {
namespace {
using Screen = ConsoleController::Screen;
using FrontView = ScriptedTestHost::FrontView;
// Three seconds of media audio.
constexpr std::uint64_t kEnoughMediaBytes = 48000 * 2 * 2 * 3;

// The touch step of the --test-keys script, "t:X:Y".
bool ParseTapStep(const std::string& step, int& x, int& y)
{
    const char* const end = step.data() + step.size();
    const auto first = std::from_chars(step.data() + 2, end, x);
    if (first.ec != std::errc{} || first.ptr == end || *first.ptr != ':') return false;
    return std::from_chars(first.ptr + 1, end, y).ec == std::errc{};
}

// The controller key a "c:name" step names.
std::optional<ConsoleKey> ConsoleKeyNamed(std::string_view name)
{
    static const std::pair<const char*, ConsoleKey> kNames[] = {{"home", ConsoleKey::Home}, {"menu", ConsoleKey::Menu}, {"option", ConsoleKey::Option},
        {"media", ConsoleKey::Media}, {"radio", ConsoleKey::Radio}, {"tel", ConsoleKey::Tel}, {"nav", ConsoleKey::Nav}, {"map", ConsoleKey::Map},
        {"back", ConsoleKey::Back}, {"projection", ConsoleKey::Projection}};
    for (const auto& [text, key] : kNames) {
        if (name == text) return key;
    }
    return std::nullopt;
}

// The --test-keys script: the steps of HEADUNIT_TEST_KEYS, separated by commas.
std::vector<std::string> KeysScript()
{
    std::vector<std::string> steps;
    const std::string script = GetEnv("HEADUNIT_TEST_KEYS").value_or("");
    std::size_t start = 0;
    while (start <= script.size()) {
        const std::size_t comma = std::min(script.find(',', start), script.size());
        std::string step = script.substr(start, comma - start);
        const auto first = step.find_first_not_of(" \t");
        const auto last = step.find_last_not_of(" \t");
        if (first != std::string::npos) steps.push_back(step.substr(first, last - first + 1));
        start = comma + 1;
    }
    return steps;
}
}

// A test of `kind` that reports through `host`; it starts once the phone's picture runs.
ScriptedPhoneTest::ScriptedPhoneTest(Kind kind, ScriptedTestHost& host) : m_kind(kind), m_host(host)
{
}

// One step of the test (called every 33 ms while the connection runs and the test has not finished).
void ScriptedPhoneTest::Tick()
{
    switch (m_kind) {
    case Kind::Input: RunInputTest(); break;
    case Kind::Audio: RunAudioTest(); break;
    case Kind::Console: RunConsoleTest(); break;
    case Kind::Keys: RunKeysTest(); break;
    }
}

// Milliseconds since the current stage began (0 before the first).
qint64 ScriptedPhoneTest::Elapsed() const
{
    return m_clock.isValid() ? m_clock.elapsed() : 0;
}

// Moves on to `stage`, whose time starts now.
void ScriptedPhoneTest::NextStage(int stage)
{
    m_stage = stage;
    m_clock.restart();
}

// Whether the phone's picture runs and the session takes input.
bool ScriptedPhoneTest::IsPictureReady() const
{
    return m_host.DisplayedFrames() >= kProjectionTestFrames && m_host.PhoneInput().IsAttached();
}

// Sends rotary, key and touch input to the real phone and counts how many new picture frames it draws in response. A
// phone that ignores the input draws none.
void ScriptedPhoneTest::RunInputTest()
{
    auto& input = m_host.PhoneInput();
    const DisplayConfig display = m_host.AnnouncedDisplay();
    const int centreX = VideoLayoutOf(display).width / 2;
    const int centreY = VideoLayoutOf(display).height / 2;
    const qint64 elapsed = Elapsed();
    switch (m_stage) {
    case 0:   // wait for the picture
        if (IsPictureReady()) NextStage(1);
        break;
    case 1:   // let the phone settle
        if (elapsed < 2500) break;
        m_frames[0] = m_host.DisplayedFrames();
        NextStage(2);
        break;
    case 2:   // idle baseline, then rotary + directional keys
        if (elapsed < 1500) break;
        m_frames[1] = m_host.DisplayedFrames();
        m_host.SaveTestShot("input-1-before.png");
        m_host.SaveTestShot("window.png", true);
        input.Rotate(+1);
        input.Rotate(+1);
        input.Rotate(-1);
        input.Tap(keys::DpadDown);
        input.Tap(keys::DpadUp);
        NextStage(3);
        break;
    case 3:   // touch tap in the middle of the screen
        if (elapsed < 2500) break;
        m_frames[2] = m_host.DisplayedFrames();
        m_host.SaveTestShot("input-2-after-rotary.png");
        input.Touch(TouchAction::Down, centreX, centreY);
        NextStage(4);
        break;
    case 4:
        if (elapsed < 120) break;
        input.Touch(TouchAction::Up, centreX, centreY);
        NextStage(5);
        break;
    case 5:   // go back to the home screen
        if (elapsed < 2500) break;
        m_frames[3] = m_host.DisplayedFrames();
        m_host.SaveTestShot("input-3-after-touch.png");
        input.Tap(keys::Home);
        NextStage(6);
        break;
    case 6: {
        if (elapsed < 1200) break;
        const unsigned idle = m_frames[1] - m_frames[0];
        const unsigned rotary = m_frames[2] - m_frames[1];
        const unsigned touch = m_frames[3] - m_frames[2];
        m_host.FinishTest(rotary + touch > 0 ? 0 : 4, "Input test: frames while idle=" + std::to_string(idle) + ", after rotary/keys=" +
            std::to_string(rotary) + ", after touch tap=" + std::to_string(touch));
        break;
    }
    default: break;
    }
}

// Starts playback on the phone and checks that PCM reaches the speaker output. The volume is set very low first, so that
// a phone that starts music does not blast it.
void ScriptedPhoneTest::RunAudioTest()
{
    auto& audio = m_host.Audio();
    auto& input = m_host.PhoneInput();
    if (m_stage == 0) {
        if (m_host.DisplayedFrames() < 5 || !input.IsAttached()) return;
        audio.SetMuted(false);
        audio.SetVolume(5);
        input.Tap(keys::MediaPlay);
        NextStage(1);
        return;
    }
    const auto media = audio.BytesRendered(AudioKind::Media);
    const std::string counts = "media=" + std::to_string(media) + " (sent by phone: " + std::to_string(audio.BytesPlayed(AudioKind::Media)) +
        ") guidance=" + std::to_string(audio.BytesRendered(AudioKind::Guidance)) + " system=" + std::to_string(audio.BytesRendered(AudioKind::System)) +
        " bytes played";
    if (media >= kEnoughMediaBytes) {
        input.Tap(keys::MediaPause);
        m_host.FinishTest(0, "Audio test: PCM reached the speaker output (" + counts + ")");
    } else if (Elapsed() >= 25000) {
        input.Tap(keys::MediaPause);
        m_host.FinishTest(5, "Audio test: no media audio within 25 seconds (" + counts + "); the phone may have nothing to play");
    } else if (media == 0 && Elapsed() >= 5000 * static_cast<qint64>(m_playRequests + 1)) {
        // The first Play can arrive before the phone's media app is ready for it (or find it paused by the last run): ask
        // again every 5 seconds. Play while already playing does nothing.
        ++m_playRequests;
        input.Tap(keys::MediaPlay);
    }
}

// Presses controller keys on the real phone and checks the Home logic step by step, looking at what the phone really
// shows (not just at the controller's own state): Media opens the media app, Home brings the phone to its dashboard,
// Home again reports the radio menu (a log line) and leaves the phone alone, Media leaves the dashboard again, Home
// returns to it. Nav comes last and only has to leave the radio menu: on a wide display the phone shows the map in its
// dashboard, on a narrow one as an app, so the picture is saved and read but not judged. Pictures of the phone are saved
// for a person to look at.
void ScriptedPhoneTest::RunConsoleTest()
{
    const qint64 elapsed = Elapsed();
    switch (m_stage) {
    case 0:
        if (!IsPictureReady()) break;
        m_host.PressConsole(ConsoleKey::Media);
        NextStage(1);
        break;
    case 1:   // the media app is showing; Home must bring the phone to its dashboard
        if (elapsed < 3000) break;
        m_host.SaveTestShot("console-1-media.png");
        ExpectPhone(PhoneScreen::Other, "the phone did not show an app after Media");
        m_host.PressConsole(ConsoleKey::Home);
        NextStage(2);
        break;
    case 2:   // first Home: the phone shows its dashboard, no radio menu yet
        if (elapsed < 2500) break;
        m_host.SaveTestShot("console-2-home.png");
        ExpectPhone(PhoneScreen::Dashboard, "first Home did not bring the phone to its dashboard");
        ExpectScreen(Screen::ProjectionHome, "first Home did not end on the phone's dashboard");
        if (m_host.HasLogLine("Radio-Startmenue")) m_problems += "first Home already opened the radio menu; ";
        if (!m_host.HasLogLine("Android-Auto-Startbildschirm")) m_problems += "first Home was not reported; ";
        m_host.PressConsole(ConsoleKey::Home);
        NextStage(3);
        break;
    case 3:   // second Home: the radio menu, only a log line; the phone must stay where it is
        if (elapsed < 800) break;
        m_host.SaveTestShot("console-3-second-home.png");
        m_host.SaveTestShot("console-3-home-menu.png", true);
        ExpectScreen(Screen::RadioHome, "second Home did not open the radio menu");
        ExpectFront(FrontView::HomeMenu, "second Home did not show the home menu");
        ExpectPhone(PhoneScreen::Dashboard, "second Home changed what the phone shows");
        if (!m_host.HasLogLine("Home: Radio-Startmenue")) m_problems += "radio menu line missing in the window log; ";
        m_host.PressConsole(ConsoleKey::Media);
        NextStage(4);
        break;
    case 4:   // Media launches something on the phone, so Home must go to the phone first again
        if (elapsed < 3000) break;
        m_host.SaveTestShot("console-4-media-again.png");
        ExpectScreen(Screen::Projection, "Media did not leave the dashboard");
        ExpectFront(FrontView::PhonePicture, "Media did not bring back the phone's picture");
        ExpectPhone(PhoneScreen::Other, "the phone did not show an app after Media");
        m_host.PressConsole(ConsoleKey::Home);
        NextStage(5);
        break;
    case 5:
        if (elapsed < 2500) break;
        m_host.SaveTestShot("console-5-home-again.png");
        ExpectScreen(Screen::ProjectionHome, "Home after Media did not end on the phone's dashboard");
        ExpectPhone(PhoneScreen::Dashboard, "Home after Media did not bring the phone to its dashboard");
        m_host.PressConsole(ConsoleKey::Radio);
        if (!m_host.HasLogLine("Radio: Internetradio")) m_problems += "radio key was not reported; ";
        ExpectScreen(Screen::Radio, "the radio key did not open the tuner");
        ExpectFront(FrontView::Tuner, "the radio key did not show the tuner");
        m_host.PressConsole(ConsoleKey::Nav);
        NextStage(6);
        break;
    case 6: {   // Nav leaves the radio menu for the phone; where the phone puts the map depends on the display
        if (elapsed < 3000) break;
        m_host.SaveTestShot("console-6-nav.png");
        ExpectScreen(Screen::Projection, "Nav did not leave the radio menu");
        const PhoneScreen phone = m_host.CurrentPhoneScreen();
        m_host.TestLog().Write(LogLevel::Info, "TEST", std::string("After Nav the phone shows ") +
            (phone == PhoneScreen::Dashboard ? "its dashboard (map card)" : phone == PhoneScreen::Other ? "an app (map)" : "an unknown screen"));
        if (phone == PhoneScreen::Unknown) m_problems += "the phone's picture could not be read after Nav; ";
        m_host.FinishTest(m_problems.empty() ? 0 : 6,
            m_problems.empty() ? "Console test: Home, Media, Radio and Nav behave as specified" : "Console test: " + m_problems);
        break;
    }
    default: break;
    }
}

// Diagnostic: plays the script in HEADUNIT_TEST_KEYS on the real phone and saves a picture after every step. Steps are
// separated by commas: a number taps that key code, "t:X:Y" taps the touchscreen at X,Y (0..799, 0..479), "c:name"
// presses a controller key (home, media, ...). Used to find out what a key does on a given phone.
void ScriptedPhoneTest::RunKeysTest()
{
    if (m_stage == 0) {
        if (!IsPictureReady()) return;
        m_steps = KeysScript();
        NextStage(1);
        m_host.SaveTestShot("keys-0-start.png");
        return;
    }
    const std::size_t index = static_cast<std::size_t>(m_stage - 1);
    if (Elapsed() < 2800) return;
    if (index > 0) m_host.SaveTestShot(("keys-" + std::to_string(index) + ".png").c_str());
    if (index >= m_steps.size()) {
        m_host.FinishTest(0, "Keys test: " + std::to_string(m_steps.size()) + " steps played");
        return;
    }
    m_host.TestLog().Write(LogLevel::Info, "TEST", "Keys test step " + std::to_string(index + 1) + ": " + m_steps[index]);
    PlayKeysStep(m_steps[index]);
    NextStage(m_stage + 1);
}

// Plays one step of the --test-keys script.
void ScriptedPhoneTest::PlayKeysStep(const std::string& step)
{
    auto& input = m_host.PhoneInput();
    if (step.rfind("t:", 0) == 0) {
        int x = 0;
        int y = 0;
        if (!ParseTapStep(step, x, y)) return;
        input.Touch(TouchAction::Down, x, y);
        input.Touch(TouchAction::Up, x, y);
    } else if (step.rfind("c:", 0) == 0) {
        if (const auto key = ConsoleKeyNamed(std::string_view(step).substr(2))) m_host.PressConsole(*key);
    } else {
        input.Tap(static_cast<unsigned>(std::strtoul(step.c_str(), nullptr, 10)));
    }
}

// Notes `problem` when the console is not on `screen`.
void ScriptedPhoneTest::ExpectScreen(Screen screen, const char* problem)
{
    if (m_host.ConsoleScreen() != screen) m_problems += std::string(problem) + "; ";
}

// Notes `problem` when the phone's picture does not show `phone`.
void ScriptedPhoneTest::ExpectPhone(PhoneScreen phone, const char* problem)
{
    if (m_host.CurrentPhoneScreen() != phone) m_problems += std::string(problem) + "; ";
}

// Notes `problem` when `view` is not in front.
void ScriptedPhoneTest::ExpectFront(FrontView view, const char* problem)
{
    if (m_host.FrontViewShown() != view) m_problems += std::string(problem) + "; ";
}
}
