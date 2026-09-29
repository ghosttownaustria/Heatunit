#pragma once
#include <algorithm>
#include <cmath>

namespace headunit {
// The volume bar the screen shows for a moment whenever the volume changes (drawn by VolumeOverlay): a panel near the
// bottom of the screen with a soft shadow, the speaker symbol at the left and one continuous line, lit up to the volume,
// with neither number nor visible steps (docs/design/heatunit.svg). Touching or dragging along the line sets the volume.
// Design units (the screen is 600 high and as wide as the display's shape makes it, see HomeMenuLayout.h).
inline constexpr double kVolumePanelTop = 485, kVolumePanelHeight = 80;
inline constexpr double kVolumePanelMaxWidth = 1150, kVolumeShadowMargin = 20, kVolumePanelMinMargin = 50 + kVolumeShadowMargin;
// Inside the panel: the room of the speaker symbol at the left and the margin at the right of the line.
inline constexpr double kVolumeSymbolRoom = 72, kVolumeEndRoom = 36;

// Where the panel and its line are, in units from the left edge of the screen.
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
    return {left, left + panelWidth, left + kVolumeSymbolRoom, left + panelWidth - kVolumeEndRoom};
}

// The volume a touch at `x` sets: the line's length up to it as a share of the maximum, rounded to a whole step. Left of
// the line it is 0, right of it the maximum.
inline int VolumeAt(double x, const VolumePanel& panel, int maxVolume)
{
    const double width = panel.barRight - panel.barLeft;
    if (width <= 0 || maxVolume <= 0) return 0;
    const double share = std::clamp((x - panel.barLeft) / width, 0.0, 1.0);
    return static_cast<int>(std::lround(share * maxVolume));
}
}
