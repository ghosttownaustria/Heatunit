#pragma once
#include "androidauto/DisplayConfig.h"
#include "androidauto/PhoneScreenDetector.h"
#include <string>
#include <utility>
#include <vector>

namespace headunit {
// The hard keys of the simulated controller (laid out like a BMW iDrive multimedia controller).
enum class ConsoleKey { Home, Menu, Option, Media, Radio, Tel, Nav, Map, Back, Projection };

// What one key press does: keys and taps sent to the phone, one line for the window log, and whether the projection has
// to be connected first.
struct ConsoleEffect {
    std::vector<unsigned> phoneKeys;
    // Touchscreen taps (touch coordinates), sent after the keys.
    std::vector<std::pair<int, int>> phoneTaps;
    // The phone was sent its home key and shows the app launcher; the caller looks at the picture again shortly after
    // and taps the dashboard button then.
    bool shouldRetryDashboard{};
    std::string message;
    bool shouldConnect{};
};

// Decides what the controller keys mean. The radio's own side (shown in place of the phone's picture) is its home menu
// and the pages it leads to: the music player (Multimedia), the tuner (Radio), the Bluetooth phones and the settings. Menu and the second step
// of Home bring the home menu to the front, Radio the tuner, and Media the music player while no phone is projected.
// Home and Back on one of the pages go back to the home menu.
//
// Home is a two-step key while a projection is connected. The first press brings the phone to its dashboard (the
// screen with the map, media and phone cards); a second press while the phone shows that dashboard goes on to the
// radio's home menu. Whether the phone shows its dashboard is read from its picture (see DetectPhoneScreen) and passed
// in; if that is not possible the controller falls back to what it has sent to the phone itself. Used from the GUI
// thread only.
class ConsoleController {
public:
    // What is in front: somewhere on the phone, or one of the radio's own screens.
    enum class Screen {
        Projection,      // the phone shows something; where exactly is not known
        ProjectionHome,  // the dashboard was asked for and nothing left it since
        RadioHome,       // the radio's home menu is in front of the phone
        Multimedia,      // the radio's music player (its music folder)
        Radio,           // the radio's tuner (internet radio)
        Settings,        // the radio's settings (which tiles the home menu shows)
        Bluetooth,       // the phones paired over Bluetooth; one of them can become the Android Auto phone
    };

    // Whether one of the radio's own pages is in front (not its home menu, not the phone).
    static constexpr bool IsRadioPage(Screen screen)
    {
        return screen == Screen::Multimedia || screen == Screen::Radio || screen == Screen::Settings || screen == Screen::Bluetooth;
    }
    // Whether the radio's side is in front: its home menu or one of its pages.
    static constexpr bool IsRadioScreen(Screen screen) { return screen == Screen::RadioHome || IsRadioPage(screen); }

    bool IsProjectionConnected() const;
    Screen CurrentScreen() const;
    void SetProjectionConnected(bool isConnected);
    void SetDisplay(const DisplayConfig& display);
    const DisplayConfig& Display() const;
    ConsoleEffect Press(ConsoleKey key, PhoneScreen phone = PhoneScreen::Unknown);
    ConsoleEffect Open(Screen page);
    void NoteTouch();
    void NoteKey(unsigned keycode, bool isDown);

private:
    static ConsoleEffect Message(std::string text);

    bool m_isConnected{};
    Screen m_screen{Screen::RadioHome};
    DisplayConfig m_display{kDefaultDisplay};

    void GoToDashboard(ConsoleEffect& effect, PhoneScreen phone) const;
    ConsoleEffect PressHome(PhoneScreen phone);
    ConsoleEffect PressBack();
    ConsoleEffect PressProjection();
    ConsoleEffect ToPhone(const char* name, unsigned keycode);
};
}
