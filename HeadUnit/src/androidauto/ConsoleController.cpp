#include "androidauto/ConsoleController.h"
#include "androidauto/ProjectionKeys.h"

namespace headunit {
// Whether a projection runs (its first picture has arrived).
bool ConsoleController::IsProjectionConnected() const
{
    return m_isConnected;
}

// What is in front right now.
ConsoleController::Screen ConsoleController::CurrentScreen() const
{
    return m_screen;
}

// A new projection starts on the phone; without one only the radio is left.
void ConsoleController::SetProjectionConnected(bool isConnected)
{
    m_isConnected = isConnected;
    m_screen = isConnected ? Screen::Projection : Screen::RadioHome;
}

// The display announced to the phone; the taps of a key press are in its touch coordinates.
void ConsoleController::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
}

// The display the taps are computed for.
const DisplayConfig& ConsoleController::Display() const
{
    return m_display;
}

// What `key` does now. `phone` is where the phone's picture says it is; only Home looks at it.
ConsoleEffect ConsoleController::Press(ConsoleKey key, PhoneScreen phone)
{
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

// Brings one of the radio's own screens to the front (a tile of the home menu); other screens are ignored.
ConsoleEffect ConsoleController::Open(Screen page)
{
    switch (page) {
    case Screen::Multimedia: m_screen = page; return Message("Multimedia: Musikordner");
    case Screen::Radio: m_screen = page; return Message("Radio: Internetradio");
    case Screen::Settings: m_screen = page; return Message("Settings: Kacheln des Startmenues");
    case Screen::RadioHome: m_screen = page; return Message("Radio-Startmenue");
    default: return {};
    }
}

// Input that reaches the phone without a controller key: a touch on the picture works inside the phone's screens, so
// it is no longer on its dashboard (only used when the picture cannot be read).
void ConsoleController::NoteTouch()
{
    if (m_isConnected) m_screen = Screen::Projection;
}

// Pressing the rotary's centre selects the focused item, which usually opens something.
void ConsoleController::NoteKey(unsigned keycode, bool isDown)
{
    if (m_isConnected && isDown && keycode == keys::DpadCenter) m_screen = Screen::Projection;
}

// An effect that is only a line for the window log.
ConsoleEffect ConsoleController::Message(std::string text)
{
    ConsoleEffect effect;
    effect.message = std::move(text);
    return effect;
}

// Brings the phone to its dashboard. The dashboard button is tapped when the picture shows it; when the picture is not
// readable the phone's own home key is sent and the button is tapped after a look at the resulting picture.
void ConsoleController::GoToDashboard(ConsoleEffect& effect, PhoneScreen phone) const
{
    if (phone == PhoneScreen::Other) {
        effect.phoneTaps.push_back(DashboardButtonPosition(m_display));
    } else if (phone == PhoneScreen::Unknown) {
        effect.phoneKeys.push_back(keys::Home);
        effect.shouldRetryDashboard = true;
    }
}

// Home: the radio's home menu from the radio's side or without a phone; otherwise first the phone's dashboard, then
// the radio's home menu.
ConsoleEffect ConsoleController::PressHome(PhoneScreen phone)
{
    if (IsRadioPage(m_screen) || !m_isConnected) {
        m_screen = Screen::RadioHome;
        return Message("Home: Radio-Startmenue");
    }
    const bool isAtDashboard = phone == PhoneScreen::Dashboard || (phone == PhoneScreen::Unknown && m_screen == Screen::ProjectionHome);
    if (m_screen == Screen::RadioHome) {
        // The radio menu only covered the phone, which still shows whatever it showed. Back to the phone's dashboard.
        m_screen = Screen::ProjectionHome;
        auto effect = Message("Home: zurueck zum Android-Auto-Startbildschirm");
        if (!isAtDashboard) GoToDashboard(effect, phone);
        return effect;
    }
    if (isAtDashboard) {
        m_screen = Screen::RadioHome;
        return Message("Home: Radio-Startmenue");
    }
    m_screen = Screen::ProjectionHome;
    auto effect = Message("Home: Android-Auto-Startbildschirm");
    GoToDashboard(effect, phone);
    return effect;
}

// Back: from a radio page to the radio's home menu, from the home menu to the phone, and on the phone the phone's own
// back key.
ConsoleEffect ConsoleController::PressBack()
{
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

// The projection key: connects when nothing is connected, otherwise brings the phone's picture to the front.
ConsoleEffect ConsoleController::PressProjection()
{
    if (!m_isConnected) {
        auto effect = Message("Android Auto: verbinde ...");
        effect.shouldConnect = true;
        return effect;
    }
    if (IsRadioScreen(m_screen)) m_screen = Screen::Projection;
    return Message("Android Auto: Projektion im Vordergrund");
}

// A key that belongs to the phone (`name` for the message when there is no phone); it launches something there.
ConsoleEffect ConsoleController::ToPhone(const char* name, unsigned keycode)
{
    if (!m_isConnected) return Message(std::string(name) + ": Android Auto ist nicht verbunden");
    m_screen = Screen::Projection;
    ConsoleEffect effect;
    effect.phoneKeys.push_back(keycode);
    return effect;
}
}
