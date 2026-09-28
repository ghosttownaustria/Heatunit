#include "ui/HomeMenuLayout.h"

namespace headunit {
// The tile at `x`,`y` (units on the screen, the row scrolled by `scroll`); none between, above and below the tiles.
std::optional<int> HomeTileAt(double x, double y, double scroll, int count)
{
    if (y < kHomeTileTop || y >= kHomeTileTop + kHomeTileHeight) return std::nullopt;
    const double inRow = x + scroll - kHomeTileMargin;
    if (inRow < 0) return std::nullopt;
    const int index = static_cast<int>(inRow / kHomeTilePitch);
    if (index >= count || inRow - index * kHomeTilePitch >= kHomeTileWidth) return std::nullopt;
    return index;
}
}
