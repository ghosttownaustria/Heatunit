#include "androidauto/TouchMapping.h"
#include <algorithm>

namespace headunit {
// Maps a position in a widget that shows the picture (the shown area of the video) letterboxed (aspect ratio kept,
// centred) to coordinates of the touchscreen announced to the phone for `display`, which are pixels of that shown area.
// Outside the picture there is no position, unless `isClamped` is set (a finger that is already down keeps reporting
// when it leaves the picture).
std::optional<std::pair<int, int>> MapToTouch(int widgetWidth, int widgetHeight, int videoWidth, int videoHeight, double x, double y,
    bool isClamped, const DisplayConfig& display)
{
    if (widgetWidth <= 0 || widgetHeight <= 0 || videoWidth <= 0 || videoHeight <= 0) return std::nullopt;
    const double scale = std::min(static_cast<double>(widgetWidth) / videoWidth, static_cast<double>(widgetHeight) / videoHeight);
    const double shownWidth = videoWidth * scale;
    const double shownHeight = videoHeight * scale;
    const double left = (widgetWidth - shownWidth) / 2;
    const double top = (widgetHeight - shownHeight) / 2;
    double relativeX = (x - left) / shownWidth;
    double relativeY = (y - top) / shownHeight;
    if (!isClamped && (relativeX < 0 || relativeX >= 1 || relativeY < 0 || relativeY >= 1)) return std::nullopt;
    relativeX = std::clamp(relativeX, 0.0, 1.0);
    relativeY = std::clamp(relativeY, 0.0, 1.0);
    const auto touchscreen = VideoLayoutOf(display);
    const int touchX = std::min(touchscreen.width - 1, static_cast<int>(relativeX * touchscreen.width));
    const int touchY = std::min(touchscreen.height - 1, static_cast<int>(relativeY * touchscreen.height));
    return std::make_pair(touchX, touchY);
}
}
