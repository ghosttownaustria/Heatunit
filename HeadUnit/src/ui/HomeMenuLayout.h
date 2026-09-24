#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include <algorithm>
#include <iterator>
#include <optional>

namespace headunit {
// The radio's home menu: the clock and one row of tiles, the focused one lit up (drawn by HomeMenu). The layout
// follows the 1600x600 design (docs/design/home-menu.svg) in design units: the screen is 600 units high and as wide
// as the display's shape makes it (1600 on the 1600x600 display, 1000 on 800x480, 1067 on 16:9), so every display
// shows the tiles at the same proportions and a wider display more of the row. The row scrolls to keep the focused
// tile in view.
enum class HomeMenuEntry { Multimedia, Radio, Telephone, Navigation, Vehicle, Settings };
inline constexpr HomeMenuEntry kHomeMenuEntries[] = {HomeMenuEntry::Multimedia, HomeMenuEntry::Radio, HomeMenuEntry::Telephone,
    HomeMenuEntry::Navigation, HomeMenuEntry::Vehicle, HomeMenuEntry::Settings};
inline constexpr int kHomeMenuCount = static_cast<int>(std::size(kHomeMenuEntries));

constexpr const char* HomeMenuTitle(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Multimedia: return "Multimedia";
    case HomeMenuEntry::Radio: return "Radio";
    case HomeMenuEntry::Telephone: return "Telephone";
    case HomeMenuEntry::Navigation: return "Navigation";
    case HomeMenuEntry::Vehicle: return "Vehicle";
    case HomeMenuEntry::Settings: return "Settings";
    }
    return "";
}

// The radio's own page a tile opens: its music player and its tuner.
constexpr std::optional<ConsoleController::Screen> HomeMenuPage(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Multimedia: return ConsoleController::Screen::Multimedia;
    case HomeMenuEntry::Radio: return ConsoleController::Screen::Radio;
    default: return std::nullopt;
    }
}
// The controller key the phone's tiles stand for: opening the tile is pressing that key. Vehicle and Settings have
// neither a page nor a key yet.
constexpr std::optional<ConsoleKey> HomeMenuKey(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Telephone: return ConsoleKey::Tel;
    case HomeMenuEntry::Navigation: return ConsoleKey::Nav;
    default: return std::nullopt;
    }
}

// Every tile is 250 x 400 units, 100 units below the top edge; the row starts 25 units from the left edge and the
// tiles are 300 units apart.
inline constexpr double kHomeMenuHeight = 600;
inline constexpr double kHomeTileTop = 100, kHomeTileWidth = 250, kHomeTileHeight = 400;
inline constexpr double kHomeTileMargin = 25, kHomeTilePitch = 300;

// Width of the screen in design units.
constexpr double HomeMenuWidth(const DisplayConfig& display)
{
    return display.height > 0 ? kHomeMenuHeight * display.width / display.height : 0;
}
// Left edge of a tile in the unscrolled row.
constexpr double HomeTileLeft(int index) { return kHomeTileMargin + index * kHomeTilePitch; }
// The whole row, with the margin at both ends.
inline constexpr double kHomeRowWidth = HomeTileLeft(kHomeMenuCount - 1) + kHomeTileWidth + kHomeTileMargin;

// How far the row is scrolled (units; 0 = the first tile at the left edge) so that tile `focus` and its margin are
// in view on a screen `width` units wide. Starting from `scroll`, it moves only as far as needed and never beyond
// either end of the row.
inline double HomeMenuScroll(int focus, double width, double scroll)
{
    const double left = HomeTileLeft(focus) - kHomeTileMargin;
    const double right = HomeTileLeft(focus) + kHomeTileWidth + kHomeTileMargin;
    if (right - scroll > width) scroll = right - width;
    if (left < scroll) scroll = left;
    return std::clamp(scroll, 0.0, std::max(0.0, kHomeRowWidth - width));
}

// The tile at `x`,`y` (units on the screen, the row scrolled by `scroll`); none between, above and below the tiles.
inline std::optional<int> HomeTileAt(double x, double y, double scroll)
{
    if (y < kHomeTileTop || y >= kHomeTileTop + kHomeTileHeight) return std::nullopt;
    const double inRow = x + scroll - kHomeTileMargin;
    if (inRow < 0) return std::nullopt;
    const int index = static_cast<int>(inRow / kHomeTilePitch);
    if (index >= kHomeMenuCount || inRow - index * kHomeTilePitch >= kHomeTileWidth) return std::nullopt;
    return index;
}

// The focus after moving `steps` tiles (positive: to the right); it stops at both ends of the row.
constexpr int MoveHomeFocus(int focus, int steps) { return std::clamp(focus + steps, 0, kHomeMenuCount - 1); }

// The first row a list of `count` rows shows, `visible` at a time, so that row `focus` is in view; starting from
// `first` it moves as little as possible.
constexpr int ListFirstRow(int focus, int first, int visible, int count)
{
    if (count <= 0 || visible <= 0) return 0;
    if (focus < first) first = focus;
    if (focus >= first + visible) first = focus - visible + 1;
    return std::clamp(first, 0, std::max(0, count - visible));
}

// Where the controller is on a player page (Multimedia, Radio): a row of buttons at the top (header), the player's
// controls below the title and the list. Turning moves within the part; up and down go through the list and from part
// to part; left and right go along a row of buttons.
struct PageFocus {
    enum class Part { Header, Controls, List };
    Part part{Part::List};
    int button{};   // in the header or the controls
    int row{};      // in the list; kept while the focus is elsewhere
    friend bool operator==(const PageFocus&, const PageFocus&) = default;
};
struct PageShape {
    int headerButtons{}, controlButtons{}, rows{};
};
// The focus made valid again after the page changed (the list got shorter or empty, ...).
constexpr PageFocus FitFocus(PageFocus focus, const PageShape& shape)
{
    focus.row = shape.rows > 0 ? std::clamp(focus.row, 0, shape.rows - 1) : 0;
    if (focus.part == PageFocus::Part::List && shape.rows == 0) { focus.part = PageFocus::Part::Controls; focus.button = shape.controlButtons / 2; }
    if (focus.part == PageFocus::Part::Header && shape.headerButtons == 0) focus.part = PageFocus::Part::Controls;
    const int buttons = focus.part == PageFocus::Part::Header ? shape.headerButtons : shape.controlButtons;
    focus.button = std::clamp(focus.button, 0, std::max(0, buttons - 1));
    return focus;
}
constexpr PageFocus TurnFocus(PageFocus focus, const PageShape& shape, int steps)
{
    focus = FitFocus(focus, shape);
    if (focus.part == PageFocus::Part::List) focus.row = std::clamp(focus.row + steps, 0, std::max(0, shape.rows - 1));
    else focus.button += steps;
    return FitFocus(focus, shape);
}
constexpr PageFocus NudgeFocus(PageFocus focus, const PageShape& shape, unsigned keycode)
{
    using Part = PageFocus::Part;
    focus = FitFocus(focus, shape);
    switch (keycode) {
    case keys::DpadUp:
        if (focus.part == Part::List && focus.row > 0) --focus.row;
        else if (focus.part == Part::List) { focus.part = Part::Controls; focus.button = shape.controlButtons / 2; }
        else if (focus.part == Part::Controls && shape.headerButtons > 0) { focus.part = Part::Header; focus.button = 0; }
        break;
    case keys::DpadDown:
        if (focus.part == Part::Header) { focus.part = Part::Controls; focus.button = shape.controlButtons / 2; }
        else if (focus.part == Part::Controls && shape.rows > 0) focus.part = Part::List;
        else if (focus.part == Part::List) ++focus.row;
        break;
    case keys::DpadLeft:
        if (focus.part != Part::List) --focus.button;
        break;
    case keys::DpadRight:
        if (focus.part != Part::List) ++focus.button;
        break;
    default:
        break;
    }
    return FitFocus(focus, shape);
}
}
