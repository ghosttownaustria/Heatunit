#pragma once
#include "androidauto/DisplayConfig.h"
#include "androidauto/PhoneScreenDetector.h"
#include "androidauto/ProjectionInput.h"
#include <string>
#include <utility>
#include <vector>

namespace headunit {
// The hard keys of the simulated controller (laid out like a BMW iDrive multimedia controller).
enum class ConsoleKey { Home, Menu, Option, Media, Radio, Tel, Nav, Map, Back, Projection };

// What one key press does: keys and taps sent to the phone, one line for the window log, and whether the
// projection has to be connected first.
struct ConsoleEffect {
    std::vector<unsigned> phoneKeys;
    // Touchscreen taps (touch coordinates), sent after the keys.
    std::vector<std::pair<int, int>> phoneTaps;
    // The phone was sent its home key and shows the app launcher; the caller looks at the picture again
    // shortly after and taps the dashboard button then.
    bool retryDashboard{};
    std::string message;
    bool connect{};
};

// Decides what the controller keys mean. The radio's own side (shown in place of the phone's picture) is its
// home menu and the pages it leads to: the music player (Multimedia), the tuner (Radio) and the settings. Menu and the second
// step of Home bring the home menu to the front, Radio the tuner, and Media the music player while no phone is
// projected. Home and Back on one of the pages go back to the home menu.
//
// Home is a two-step key while a projection is connected. The first press brings the phone to its
// dashboard (the screen with the map, media and phone cards); a second press while the phone shows that
// dashboard goes on to the radio's home menu. Whether the phone shows its dashboard is read from its
// picture (see DetectPhoneScreen) and passed in; if that is not possible the controller falls back to
// what it has sent to the phone itself. Used from the GUI thread only.
class ConsoleController {
public:
    enum class Screen {
        Projection,      // the phone shows something; where exactly is not known
        ProjectionHome,  // the dashboard was asked for and nothing left it since
        RadioHome,       // the radio's home menu is in front of the phone
        Multimedia,      // the radio's music player (its music folder)
        Radio,           // the radio's tuner (internet radio)
        Settings,        // the radio's settings (which tiles the home menu shows)
    };
    // The radio's side: one of its own pages is in front, not the phone.
    static constexpr bool IsRadioScreen(Screen screen) { return screen == Screen::RadioHome || IsRadioPage(screen); }
    static constexpr bool IsRadioPage(Screen screen) { return screen == Screen::Multimedia || screen == Screen::Radio || screen == Screen::Settings; }
    bool IsProjectionConnected() const { return m_isConnected; }
    Screen CurrentScreen() const { return m_screen; }

    // A new projection starts on the phone; without one only the radio is left.
    void SetProjectionConnected(bool isConnected) {
        m_isConnected = isConnected;
        m_screen = isConnected ? Screen::Projection : Screen::RadioHome;
    }
    // The display announced to the phone; the taps of a key press are in its touch coordinates.
    void SetDisplay(const DisplayConfig& display) { m_display = display; }
    const DisplayConfig& Display() const { return m_display; }

    // `phone` is where the phone's picture says it is; only Home looks at it.
    ConsoleEffect Press(ConsoleKey key, PhoneScreen phone = PhoneScreen::Unknown) {
        switch (key) {
        case ConsoleKey::Home: return PressHome(phone);
        case ConsoleKey::Menu:
            m_screen = Screen::RadioHome;
            return Message("Menue: Radio-Startmenue");
        case ConsoleKey::Radio: return Open(Screen::Radio);
        // Without a phone the car's own music is the media source.
        case ConsoleKey::Media: return m_isConnected ? ToPhone("Medien", keys::Media) : Open(Screen::Multimedia);
        case ConsoleKey::Tel: return ToPhone("Tel", keys::Tel);
        case ConsoleKey::Nav: return ToPhone("Nav", keys::Navigation);
        // The phone has one navigation key; Map and Nav both open its navigation app.
        case ConsoleKey::Map: return ToPhone("Map", keys::Navigation);
        case ConsoleKey::Option: return ToPhone("Option", keys::Menu);
        case ConsoleKey::Back: return PressBack();
        case ConsoleKey::Projection: return PressProjection();
        }
        return {};
    }
    // Brings one of the radio's own pages to the front (a tile of the home menu).
    ConsoleEffect Open(Screen page) {
        switch (page) {
        case Screen::Multimedia: m_screen = page; return Message("Multimedia: Musikordner");
        case Screen::Radio: m_screen = page; return Message("Radio: Internetradio");
        case Screen::Settings: m_screen = page; return Message("Settings: Kacheln des Startmenues");
        case Screen::RadioHome: m_screen = page; return Message("Radio-Startmenue");
        default: return {};
        }
    }
    // Input that reaches the phone without a controller key: a touch on the picture works inside the
    // phone's screens, so it is no longer on its dashboard (only used when the picture cannot be read).
    void NoteTouch() { if (m_isConnected) m_screen = Screen::Projection; }
    // Pressing the rotary's centre selects the focused item, which usually opens something.
    void NoteKey(unsigned keycode, bool isDown) {
        if (m_isConnected && isDown && keycode == keys::DpadCenter) m_screen = Screen::Projection;
    }
private:
    static ConsoleEffect Message(std::string text) {
        ConsoleEffect effect;
        effect.message = std::move(text);
        return effect;
    }
    static ConsoleEffect Radio(const char* text) { return Message(text); }
    // Brings the phone to its dashboard. The dashboard button is tapped when the picture shows it; when
    // the picture is not readable the phone's own home key is sent and the button is tapped after a look
    // at the resulting picture.
    void GoToDashboard(ConsoleEffect& effect, PhoneScreen phone) const {
        if (phone == PhoneScreen::Other) {
            effect.phoneTaps.push_back(DashboardButtonPosition(m_display));
        } else if (phone == PhoneScreen::Unknown) {
            effect.phoneKeys.push_back(keys::Home);
            effect.retryDashboard = true;
        }
    }
    ConsoleEffect PressHome(PhoneScreen phone) {
        if (IsRadioPage(m_screen) || !m_isConnected) {
            m_screen = Screen::RadioHome;
            return Radio("Home: Radio-Startmenue");
        }
        const bool isAtDashboard = phone == PhoneScreen::Dashboard || (phone == PhoneScreen::Unknown && m_screen == Screen::ProjectionHome);
        if (m_screen == Screen::RadioHome) {
            // The radio menu only covered the phone, which still shows whatever it showed. Back to the
            // phone's dashboard.
            m_screen = Screen::ProjectionHome;
            auto effect = Message("Home: zurueck zum Android-Auto-Startbildschirm");
            if (!isAtDashboard) GoToDashboard(effect, phone);
            return effect;
        }
        if (isAtDashboard) {
            m_screen = Screen::RadioHome;
            return Radio("Home: Radio-Startmenue");
        }
        m_screen = Screen::ProjectionHome;
        auto effect = Message("Home: Android-Auto-Startbildschirm");
        GoToDashboard(effect, phone);
        return effect;
    }
    ConsoleEffect PressBack() {
        if (IsRadioPage(m_screen)) {
            m_screen = Screen::RadioHome;
            return Message("Back: zurueck zum Radio-Startmenue");
        }
        if (!m_isConnected) return Message("Back: nichts zu tun, Android Auto ist nicht verbunden");
        if (m_screen == Screen::RadioHome) {
            m_screen = Screen::Projection;
            return Message("Back: Radio-Startmenue verlassen, zurueck zu Android Auto");
        }
        ConsoleEffect effect;   // a plain key on the phone; it never changes where the phone is
        effect.phoneKeys.push_back(keys::Back);
        return effect;
    }
    ConsoleEffect PressProjection() {
        if (!m_isConnected) {
            auto effect = Message("Android Auto: verbinde ...");
            effect.connect = true;
            return effect;
        }
        if (IsRadioScreen(m_screen)) m_screen = Screen::Projection;
        return Message("Android Auto: Projektion im Vordergrund");
    }
    ConsoleEffect ToPhone(const char* name, unsigned keycode) {
        if (!m_isConnected) return Message(std::string(name) + ": Android Auto ist nicht verbunden");
        m_screen = Screen::Projection;   // launches something on the phone
        ConsoleEffect effect;
        effect.phoneKeys.push_back(keycode);
        return effect;
    }
    bool m_isConnected{};
    Screen m_screen{Screen::RadioHome};
    DisplayConfig m_display{kDefaultDisplay};
};
}
