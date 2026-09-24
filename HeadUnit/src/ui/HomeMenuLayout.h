#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include <algorithm>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace headunit {
// The radio's home menu: the clock, one row of tiles (the focused one lit up) and a bar at the bottom that shows which
// part of the row is in view (drawn by HomeMenu). The layout follows the 1600x600 design (docs/design/home-menu.svg) in
// design units: the screen is 600 units high and as wide as the display's shape makes it (1600 on the 1600x600 display,
// 1000 on 800x480, 1067 on 16:9), so every display shows the tiles at the same proportions and a wider display more of
// the row. The focused tile stays in the middle of the screen, except near either end of the row; an arrow at an edge
// says that more tiles follow that way. Which tiles show, and in which order, the user decides (HomeTileSetup).
enum class HomeMenuEntry { AndroidAuto, Multimedia, Radio, Telephone, Navigation, Vehicle, Settings };
// Every tile there is, in the order a new radio shows them.
inline constexpr HomeMenuEntry kHomeMenuEntries[] = {HomeMenuEntry::AndroidAuto, HomeMenuEntry::Multimedia, HomeMenuEntry::Radio,
    HomeMenuEntry::Telephone, HomeMenuEntry::Navigation, HomeMenuEntry::Vehicle, HomeMenuEntry::Settings};
inline constexpr int kHomeMenuCount = static_cast<int>(std::size(kHomeMenuEntries));

constexpr const char* HomeMenuTitle(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return "Android Auto";
    case HomeMenuEntry::Multimedia: return "Multimedia";
    case HomeMenuEntry::Radio: return "Radio";
    case HomeMenuEntry::Telephone: return "Telephone";
    case HomeMenuEntry::Navigation: return "Navigation";
    case HomeMenuEntry::Vehicle: return "Vehicle";
    case HomeMenuEntry::Settings: return "Settings";
    }
    return "";
}
// The name a tile is remembered by (TileSetupText).
constexpr const char* HomeMenuId(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return "AndroidAuto";
    default: return HomeMenuTitle(entry);
    }
}

// The radio's own page a tile opens: its music player, its tuner and its settings.
constexpr std::optional<ConsoleController::Screen> HomeMenuPage(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Multimedia: return ConsoleController::Screen::Multimedia;
    case HomeMenuEntry::Radio: return ConsoleController::Screen::Radio;
    case HomeMenuEntry::Settings: return ConsoleController::Screen::Settings;
    default: return std::nullopt;
    }
}
// The controller key the phone's tiles stand for: opening the tile is pressing that key (Android Auto connects or brings
// the phone to the front, as the projection key does). Vehicle has neither a page nor a key yet.
constexpr std::optional<ConsoleKey> HomeMenuKey(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return ConsoleKey::Projection;
    case HomeMenuEntry::Telephone: return ConsoleKey::Tel;
    case HomeMenuEntry::Navigation: return ConsoleKey::Nav;
    default: return std::nullopt;
    }
}

// Which tiles the home menu shows, and in which order: every tile once, each shown or hidden. Settings is always shown,
// it is the way back to the others.
struct HomeTileSetup {
    struct Tile {
        HomeMenuEntry entry{};
        bool isShown{true};
        friend bool operator==(const Tile&, const Tile&) = default;
    };
    std::vector<Tile> tiles;
    friend bool operator==(const HomeTileSetup&, const HomeTileSetup&) = default;
};
inline HomeTileSetup DefaultTileSetup()
{
    HomeTileSetup setup;
    for (const HomeMenuEntry entry : kHomeMenuEntries) setup.tiles.push_back({entry, true});
    return setup;
}
// The tiles the home menu shows, in their order.
inline std::vector<HomeMenuEntry> ShownTiles(const HomeTileSetup& setup)
{
    std::vector<HomeMenuEntry> shown;
    for (const auto& tile : setup.tiles)
        if (tile.isShown) shown.push_back(tile.entry);
    return shown;
}
// Shows or hides a tile; false when nothing changed (Settings cannot be hidden).
inline bool SetTileShown(HomeTileSetup& setup, HomeMenuEntry entry, bool isShown)
{
    if (entry == HomeMenuEntry::Settings && !isShown) return false;
    for (auto& tile : setup.tiles) {
        if (tile.entry != entry || tile.isShown == isShown) continue;
        tile.isShown = isShown;
        return true;
    }
    return false;
}
// Moves `entry` one place along the order: `direction` -1 towards the front, +1 towards the back. `isAmongShown` (the
// home menu, which does not show hidden tiles): it passes the next shown tile, and any hidden ones on the way; otherwise
// (the settings list, which shows them all) its direct neighbour. False at an end.
inline bool MoveTile(HomeTileSetup& setup, HomeMenuEntry entry, int direction, bool isAmongShown)
{
    auto& tiles = setup.tiles;
    const auto found = std::find_if(tiles.begin(), tiles.end(), [entry](const auto& tile) { return tile.entry == entry; });
    if (found == tiles.end() || direction == 0) return false;
    const int count = static_cast<int>(tiles.size());
    const int step = direction > 0 ? 1 : -1;
    const int index = static_cast<int>(found - tiles.begin());
    int other = index + step;
    while (isAmongShown && other >= 0 && other < count && !tiles[static_cast<std::size_t>(other)].isShown) other += step;
    if (other < 0 || other >= count) return false;
    const HomeTileSetup::Tile moved = *found;
    tiles.erase(found);
    tiles.insert(tiles.begin() + other, moved);
    return true;
}
// The setup as text for the settings store: the tiles' names in their order, hidden ones marked with "-"
// ("AndroidAuto,Radio,-Vehicle,...").
inline std::string TileSetupText(const HomeTileSetup& setup)
{
    std::string text;
    for (const auto& tile : setup.tiles) {
        if (!text.empty()) text += ',';
        if (!tile.isShown) text += '-';
        text += HomeMenuId(tile.entry);
    }
    return text;
}
// Reads the text back. Unknown and repeated names are skipped; tiles the text does not name (new in this version) are
// added at the end, shown; Settings is shown whatever the text says. An empty text gives the default setup.
inline HomeTileSetup ParseTileSetup(std::string_view text)
{
    HomeTileSetup setup;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string_view name = text.substr(0, comma);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
        while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
        const bool isShown = name.empty() || name.front() != '-';
        if (!isShown) name.remove_prefix(1);
        for (const HomeMenuEntry entry : kHomeMenuEntries) {
            const bool isKnown = std::any_of(setup.tiles.begin(), setup.tiles.end(), [entry](const auto& tile) { return tile.entry == entry; });
            if (name == HomeMenuId(entry) && !isKnown) setup.tiles.push_back({entry, isShown || entry == HomeMenuEntry::Settings});
        }
    }
    for (const HomeMenuEntry entry : kHomeMenuEntries)
        if (std::none_of(setup.tiles.begin(), setup.tiles.end(), [entry](const auto& tile) { return tile.entry == entry; }))
            setup.tiles.push_back({entry, true});
    return setup;
}

// Every tile is 250 x 400 units, 100 units below the top edge; the row starts 25 units from the left edge and the
// tiles are 300 units apart.
inline constexpr double kHomeMenuHeight = 600;
inline constexpr double kHomeTileTop = 100, kHomeTileWidth = 250, kHomeTileHeight = 400;
inline constexpr double kHomeTileMargin = 25, kHomeTilePitch = 300;
// An edge beyond which more tiles follow is a black strip 100 units wide with an arrow; the tiles fade into it over
// 44 units.
inline constexpr double kHomeEdgeWidth = 100, kHomeEdgeFade = 44;
// The bar at the bottom runs 25 units in from both sides at this height.
inline constexpr double kHomeBarY = 550;

// Width of the screen in design units.
constexpr double HomeMenuWidth(const DisplayConfig& display)
{
    return display.height > 0 ? kHomeMenuHeight * display.width / display.height : 0;
}
// Left edge of a tile in the unscrolled row.
constexpr double HomeTileLeft(int index) { return kHomeTileMargin + index * kHomeTilePitch; }
// A row of `count` tiles, with the margin at both ends.
constexpr double HomeRowWidth(int count) { return count > 0 ? HomeTileLeft(count - 1) + kHomeTileWidth + kHomeTileMargin : 0; }

// How far a row of `count` tiles is scrolled (units; 0 = the first tile at the left edge) so that tile `focus` is in the
// middle of a screen `width` units wide. Near either end of the row it stops at that end, so no empty space shows.
constexpr double HomeMenuScroll(int focus, double width, int count)
{
    const double centred = HomeTileLeft(focus) + kHomeTileWidth / 2 - width / 2;
    return std::clamp(centred, 0.0, std::max(0.0, HomeRowWidth(count) - width));
}
// Whether the row goes on beyond the left and the right edge of the screen: an arrow shows there.
struct HomeEdges {
    bool isLeft{}, isRight{};
};
constexpr HomeEdges HomeMenuEdges(double scroll, double width, int count)
{
    return {scroll > 0.5, HomeRowWidth(count) - width - scroll > 0.5};
}
// The lit part of the bar at the bottom: which part of the row is in view, on a track from 25 units in on the left to
// 25 units in on the right (the whole track when the row fits on the screen).
struct HomeBar {
    double from{}, to{};
};
constexpr HomeBar HomeMenuBar(double scroll, double width, int count)
{
    const double track = width - 2 * kHomeTileMargin, row = HomeRowWidth(count);
    if (row <= width) return {kHomeTileMargin, kHomeTileMargin + track};
    return {kHomeTileMargin + track * scroll / row, kHomeTileMargin + track * (scroll + width) / row};
}
// The tile at `x`,`y` (units on the screen, the row scrolled by `scroll`); none between, above and below the tiles.
inline std::optional<int> HomeTileAt(double x, double y, double scroll, int count)
{
    if (y < kHomeTileTop || y >= kHomeTileTop + kHomeTileHeight) return std::nullopt;
    const double inRow = x + scroll - kHomeTileMargin;
    if (inRow < 0) return std::nullopt;
    const int index = static_cast<int>(inRow / kHomeTilePitch);
    if (index >= count || inRow - index * kHomeTilePitch >= kHomeTileWidth) return std::nullopt;
    return index;
}

// The focus after moving `steps` tiles (positive: to the right) in a row of `count`; it stops at both ends.
constexpr int MoveHomeFocus(int focus, int steps, int count) { return std::clamp(focus + steps, 0, std::max(0, count - 1)); }

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
// controls below the title and the list. Turning the knob and pushing its arrows do different things: turning moves
// within the part the focus is in (through the list, along a row of buttons); up and down jump from part to part
// (list, controls, top buttons) wherever in the list the focus is. Left and right do not move the focus at all (the
// page uses them to skip to the previous or next title).
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
    if (keycode == keys::DpadUp) {
        if (focus.part == Part::List) { focus.part = Part::Controls; focus.button = shape.controlButtons / 2; }
        else if (focus.part == Part::Controls && shape.headerButtons > 0) { focus.part = Part::Header; focus.button = 0; }
    } else if (keycode == keys::DpadDown) {
        if (focus.part == Part::Header) { focus.part = Part::Controls; focus.button = shape.controlButtons / 2; }
        else if (focus.part == Part::Controls && shape.rows > 0) focus.part = Part::List;
    }
    return FitFocus(focus, shape);
}
}
