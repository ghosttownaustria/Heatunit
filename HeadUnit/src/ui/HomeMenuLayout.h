#pragma once
#include "androidauto/DisplayConfig.h"
#include <algorithm>
#include <optional>

namespace headunit {
// The geometry of the radio's home menu: the clock, one row of tiles (the focused one lit up) and a bar at the bottom
// that shows which part of the row is in view (drawn by HomeMenu). The layout follows the 1600x600 design
// (docs/design/home-menu.svg) in design units: the screen is 600 units high and as wide as the display's shape makes it
// (1600 on the 1600x600 display, 1000 on 800x480, 1067 on 16:9), so every display shows the tiles at the same
// proportions and a wider display more of the row. The focused tile stays in the middle of the screen, except near
// either end of the row; an arrow at an edge says that more tiles follow that way.

// Every tile is 250 x 400 units, 100 units below the top edge; the row starts 25 units from the left edge and the tiles
// are 300 units apart.
inline constexpr double kHomeMenuHeight = 600;
inline constexpr double kHomeTileTop = 100, kHomeTileWidth = 250, kHomeTileHeight = 400;
inline constexpr double kHomeTileMargin = 25, kHomeTilePitch = 300;
// An edge beyond which more tiles follow is a black strip 100 units wide with an arrow; the tiles fade into it over 44
// units.
inline constexpr double kHomeEdgeWidth = 100, kHomeEdgeFade = 44;
// The bar at the bottom runs 25 units in from both sides at this height.
inline constexpr double kHomeBarY = 550;

// Whether the row goes on beyond the left and the right edge of the screen: an arrow shows there.
struct HomeEdges {
    bool isLeft{}, isRight{};
};

// The lit part of the bar at the bottom, in units from the left edge of the screen.
struct HomeBar {
    double from{}, to{};
};

// Width of the screen in design units.
constexpr double HomeMenuWidth(const DisplayConfig& display)
{
    return display.height > 0 ? kHomeMenuHeight * display.width / display.height : 0;
}

// Left edge of a tile in the unscrolled row.
constexpr double HomeTileLeft(int index)
{
    return kHomeTileMargin + index * kHomeTilePitch;
}

// A row of `count` tiles, with the margin at both ends.
constexpr double HomeRowWidth(int count)
{
    return count > 0 ? HomeTileLeft(count - 1) + kHomeTileWidth + kHomeTileMargin : 0;
}

// How far a row of `count` tiles is scrolled (units; 0 = the first tile at the left edge) so that tile `focus` is in the
// middle of a screen `width` units wide. Near either end of the row it stops at that end, so no empty space shows.
constexpr double HomeMenuScroll(int focus, double width, int count)
{
    const double centred = HomeTileLeft(focus) + kHomeTileWidth / 2 - width / 2;
    return std::clamp(centred, 0.0, std::max(0.0, HomeRowWidth(count) - width));
}

// Which edges of the screen show an arrow for a row scrolled by `scroll`.
constexpr HomeEdges HomeMenuEdges(double scroll, double width, int count)
{
    return {scroll > 0.5, HomeRowWidth(count) - width - scroll > 0.5};
}

// Which part of the row is in view, on a track from 25 units in on the left to 25 units in on the right (the whole
// track when the row fits on the screen).
constexpr HomeBar HomeMenuBar(double scroll, double width, int count)
{
    const double track = width - 2 * kHomeTileMargin;
    const double row = HomeRowWidth(count);
    if (row <= width) return {kHomeTileMargin, kHomeTileMargin + track};
    return {kHomeTileMargin + track * scroll / row, kHomeTileMargin + track * (scroll + width) / row};
}

// The focus after moving `steps` tiles (positive: to the right) in a row of `count`; it stops at both ends.
constexpr int MoveHomeFocus(int focus, int steps, int count)
{
    return std::clamp(focus + steps, 0, std::max(0, count - 1));
}

std::optional<int> HomeTileAt(double x, double y, double scroll, int count);
}
