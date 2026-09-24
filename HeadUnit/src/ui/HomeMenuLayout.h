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

// The controller key a tile stands for: opening the tile is pressing that key. Vehicle and Settings have no key and
// nothing behind them yet.
constexpr std::optional<ConsoleKey> HomeMenuKey(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Multimedia: return ConsoleKey::Media;
    case HomeMenuEntry::Radio: return ConsoleKey::Radio;
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
}
