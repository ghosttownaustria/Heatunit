#pragma once
#include <algorithm>

namespace headunit {
// The volume bar the screen shows for a moment whenever the volume changes (drawn by VolumeOverlay): a panel near the
// bottom of the screen, framed like the tiles, with the speaker symbol at the left, one segment per volume step and the
// number at the right. Touching or dragging along the segments sets the volume. Design units (the screen is 600 high
// and as wide as the display's shape makes it, see HomeMenuLayout.h).
inline constexpr double kVolumePanelTop = 400, kVolumePanelHeight = 110;
inline constexpr double kVolumePanelMaxWidth = 1000, kVolumePanelMinMargin = 50;
// Inside the panel: the room of the speaker symbol at the left and of the number at the right of the segments.
inline constexpr double kVolumeSymbolRoom = 110, kVolumeNumberRoom = 120;

// Where the panel and its segments are, in units from the left edge of the screen.
struct VolumePanel {
    double left{}, right{};
    double barLeft{}, barRight{};
};

// The panel on a screen `width` units wide: centred, at most kVolumePanelMaxWidth wide, never closer to the edges than
// kVolumePanelMinMargin.
constexpr VolumePanel VolumePanelOf(double width)
{
    const double panelWidth = std::max(0.0, std::min(kVolumePanelMaxWidth, width - 2 * kVolumePanelMinMargin));
    const double left = (width - panelWidth) / 2;
    return {left, left + panelWidth, left + kVolumeSymbolRoom, left + panelWidth - kVolumeNumberRoom};
}

// The volume a touch at `x` sets: the segment under it lit, with all before it. Left of the first segment it is 0, right
// of the last one the maximum.
constexpr int VolumeAt(double x, const VolumePanel& panel, int maxVolume)
{
    const double width = panel.barRight - panel.barLeft;
    if (width <= 0 || maxVolume <= 0 || x <= panel.barLeft) return 0;
    const double segments = (x - panel.barLeft) / width * maxVolume;
    return std::min(maxVolume, static_cast<int>(segments) + 1);
}
}
