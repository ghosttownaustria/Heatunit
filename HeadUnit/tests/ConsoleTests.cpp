#include "CoreTestSuites.h"
#include "TestDisplays.h"
#include "TestSupport.h"
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/InputEvents.h"
#include "androidauto/PhoneScreenDetector.h"
#include "androidauto/ProjectionInput.h"
#include "androidauto/ProjectionKeys.h"
#include <atomic>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

using namespace headunit;

namespace {
// The input bus hands events to the attached session only, and survives a session detaching while the window sends.
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

// Whether `text` contains `part`.
bool Contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }
using Screen = ConsoleController::Screen;

// The hard keys, with and without a phone: which screen they open and what they send to the phone.
void TestConsoleController() {
    ConsoleController console;
    // Without a projection there is only the radio, and it has no functions yet: everything is a log line.
    auto effect = console.Press(ConsoleKey::Home);
    Check(effect.phoneKeys.empty() && Contains(effect.message, "Radio-Startmenue") && !effect.shouldConnect, "Home without a projection did not report the radio menu");
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
    effect = console.Open(Screen::Settings);
    Check(console.CurrentScreen() == Screen::Settings && !effect.message.empty(), "The Settings tile did not open the settings");
    effect = console.Press(ConsoleKey::Back);
    Check(console.CurrentScreen() == Screen::RadioHome && effect.phoneKeys.empty(), "Back on the settings did not return to the home menu");
    console.Press(ConsoleKey::Menu);
    effect = console.Open(Screen::Bluetooth);
    Check(console.CurrentScreen() == Screen::Bluetooth && !effect.message.empty(), "The Bluetooth tile did not open its page");
    Check(ConsoleController::IsRadioPage(Screen::Bluetooth) && ConsoleController::IsRadioPage(Screen::Settings) && ConsoleController::IsRadioScreen(Screen::RadioHome) && ConsoleController::IsRadioScreen(Screen::Radio) &&
        !ConsoleController::IsRadioScreen(Screen::ProjectionHome) && ConsoleController::IsRadioPage(Screen::Multimedia) &&
        !ConsoleController::IsRadioPage(Screen::RadioHome), "The radio's screens are not told apart");
    effect = console.Press(ConsoleKey::Projection);
    Check(effect.shouldConnect && effect.phoneKeys.empty(), "The projection key did not ask for a connection");

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
    Check(effect.phoneKeys.empty() && !effect.shouldConnect && console.CurrentScreen() == Screen::Projection, "The projection key did not bring the phone forward");

    // The phone goes away: only the radio is left.
    console.SetProjectionConnected(false);
    Check(console.CurrentScreen() == Screen::RadioHome && !console.IsProjectionConnected(), "Disconnecting did not fall back to the radio");
    console.NoteTouch();
    Check(console.CurrentScreen() == Screen::RadioHome, "A touch without a projection changed the screen");
}

// Home reads where the phone really is from its picture instead of guessing from what was sent.
void TestConsoleHomeWithPicture() {
    using Taps = std::vector<std::pair<int, int>>;
    const Taps dashboardTap{{kDashboardButtonX, kDashboardButtonY}};
    ConsoleController console;
    console.SetProjectionConnected(true);
    // The phone shows an app: Home taps the dashboard button (the phone's home key only opens its launcher).
    auto effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneKeys.empty() && effect.phoneTaps == dashboardTap && !effect.shouldRetryDashboard && Contains(effect.message, "Android-Auto-Startbildschirm") &&
        console.CurrentScreen() == Screen::ProjectionHome, "Home from an app did not tap the dashboard button");
    // The phone shows its dashboard: Home is the second step and leaves the phone alone.
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && Contains(effect.message, "Home: Radio-Startmenue") && console.CurrentScreen() == Screen::RadioHome,
        "Home on the dashboard did not open the radio menu");
    // Back from the radio menu: the phone still shows its dashboard, so nothing is sent.
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Dashboard);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && !effect.shouldRetryDashboard && Contains(effect.message, "zurueck") &&
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
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Home} && effect.phoneTaps.empty() && effect.shouldRetryDashboard && console.CurrentScreen() == Screen::ProjectionHome,
        "Home with an unreadable picture did not fall back to the home key and a second look");
    // Without a projection the picture is irrelevant.
    console.SetProjectionConnected(false);
    effect = console.Press(ConsoleKey::Home, PhoneScreen::Other);
    Check(effect.phoneKeys.empty() && effect.phoneTaps.empty() && Contains(effect.message, "Radio-Startmenue"), "Home without a projection used the picture");
    // Only Home looks at the picture.
    console.SetProjectionConnected(true);
    effect = console.Press(ConsoleKey::Media, PhoneScreen::Dashboard);
    Check(effect.phoneKeys == std::vector<unsigned>{keys::Media} && effect.phoneTaps.empty() && !effect.shouldRetryDashboard, "Media depended on the picture");
}

// The dashboard button sits at the same place of the phone's layout at every display size.
void TestDashboardButtonOnDisplays() {
    Check(DashboardButtonPosition(kDefaultDisplay) == std::make_pair(42, 438), "The dashboard button moved on the default display");
    Check(DashboardButtonPosition({1280, 720}) == std::make_pair(63, 657), "The dashboard button is wrong on a 1280x720 display");
    // The 1600x600 display is shown as 1920x720: the button is where it is on a 720 px high display.
    Check(DashboardButtonPosition({1600, 600}) == std::make_pair(63, 657), "The dashboard button is wrong on a 1600x600 display");
    for (const auto& display : kTestDisplays) {
        const auto [x, y] = DashboardButtonPosition(display);
        const auto layout = VideoLayoutOf(display);
        const double scale = LayoutScale(layout.height);
        // Inside the button's symbol (x 29..59, y 424..453 of the layout) and on the shown area.
        Check(x >= 29 * scale && x <= 60 * scale && y >= 424 * scale && y <= 454 * scale && x < layout.width && y < layout.height,
            "The dashboard button is off its symbol on a " + DisplayText(display) + " display");
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

// The hard keys of the console and the input bus between the window and the phone.
void RunConsoleTests()
{
    TestProjectionInput();
    TestConsoleController();
    TestConsoleHomeWithPicture();
    TestDashboardButtonOnDisplays();
}
