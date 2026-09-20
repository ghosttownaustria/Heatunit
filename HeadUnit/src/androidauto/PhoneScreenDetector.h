#pragma once
#include "androidauto/DisplayConfig.h"
#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace headunit {
// Where the phone's Android Auto currently is, as far as its picture tells.
enum class PhoneScreen {
    Unknown,    // no picture, a transition or an unfamiliar layout
    Dashboard,  // the home screen with the map, media and phone cards
    Other,      // any app, or the app launcher
};

// The button at the bottom left of Android Auto's navigation bar (position in the 800x480 layout). It
// switches between two symbols: nine dots (the launcher) while the dashboard is showing, and a framed
// split view (the dashboard) everywhere else, where tapping it goes to the dashboard. `KEYCODE_HOME`
// cannot be used for that: on current phones it always opens the app launcher.
constexpr int kDashboardButtonX = 42;
constexpr int kDashboardButtonY = 438;

// The layout is the same at every display size: the phone lays its interface out at a height of 480
// units whatever the resolution (see DisplayDensity), so every position of the 800x480 layout is scaled
// by height/480 (in both directions) and stays anchored at the left and bottom edges of the shown area.
// (On a wide display the phone moves its navigation bar to a rail at the left; the button stays at the
// bottom left.)
constexpr double LayoutScale(int height) { return height / 480.0; }

// Where the dashboard button is, in touchscreen coordinates of `display` (pixels of its shown area).
inline std::pair<int, int> DashboardButtonPosition(const DisplayConfig& display)
{
    const double scale = LayoutScale(VideoLayoutOf(display).height);
    return {static_cast<int>(kDashboardButtonX * scale + 0.5), static_cast<int>(kDashboardButtonY * scale + 0.5)};
}

// Reads the button's symbol from an RGB888 picture of the phone (any of the display sizes). This is the
// only way to learn where the phone is: the protocol never reports the screen. The symbol is told apart
// by its shape (nine small separate dots against one large connected frame), which does not depend on
// colours; a focus ring around the button reaches in from outside the inspected box and is ignored.
inline PhoneScreen DetectPhoneScreen(const std::uint8_t* rgb, int width, int height, int stride)
{
    if (!rgb || width < 200 || height < 120) return PhoneScreen::Unknown;
    const double scale = LayoutScale(height);
    const double areaScale = scale * scale;   // region sizes below are counted in 800x480 pixels
    // The symbols span x 29..59 and y 424..453; the box leaves a margin so that only things reaching in from
    // outside (a focus ring) touch its border.
    const int left = static_cast<int>(22 * scale), right = static_cast<int>(66 * scale);
    const int top = static_cast<int>(418 * scale), bottom = static_cast<int>(460 * scale);
    const int boxWidth = right - left + 1, boxHeight = bottom - top + 1;
    if (boxWidth < 8 || boxHeight < 8 || right >= width || bottom >= height) return PhoneScreen::Unknown;

    std::vector<std::uint8_t> isBright(static_cast<std::size_t>(boxWidth) * boxHeight);
    int brightCount = 0;
    for (int y = 0; y < boxHeight; ++y) {
        for (int x = 0; x < boxWidth; ++x) {
            const std::uint8_t* pixel = rgb + static_cast<std::size_t>(top + y) * stride + static_cast<std::size_t>(left + x) * 3;
            const int luminance = (299 * pixel[0] + 587 * pixel[1] + 114 * pixel[2]) / 1000;
            const bool bright = luminance > 150;
            isBright[static_cast<std::size_t>(y) * boxWidth + x] = bright;
            brightCount += bright;
        }
    }
    // A mostly bright box is a light theme or a full-screen flash: the shapes below would be inverted. (The
    // framed symbol alone covers about 40 percent of the box.)
    if (brightCount > boxWidth * boxHeight * 3 / 5) return PhoneScreen::Unknown;

    // Connected bright regions (8-neighbourhood) that lie fully inside the box.
    std::vector<int> sizes;
    std::vector<std::uint8_t> seen(isBright.size(), 0);
    std::vector<int> stack;
    for (int start = 0; start < boxWidth * boxHeight; ++start) {
        if (!isBright[start] || seen[start]) continue;
        int size = 0;
        bool touchesBorder = false;
        seen[start] = 1;
        stack.push_back(start);
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            ++size;
            const int cx = current % boxWidth, cy = current / boxWidth;
            if (cx == 0 || cy == 0 || cx == boxWidth - 1 || cy == boxHeight - 1) touchesBorder = true;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx, ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= boxWidth || ny >= boxHeight) continue;
                    const int neighbour = ny * boxWidth + nx;
                    if (!isBright[neighbour] || seen[neighbour]) continue;
                    seen[neighbour] = 1;
                    stack.push_back(neighbour);
                }
            }
        }
        if (!touchesBorder && size >= 3 * areaScale) sizes.push_back(static_cast<int>(size / areaScale + 0.5));
    }
    if (sizes.empty()) return PhoneScreen::Unknown;
    const int largest = *std::max_element(sizes.begin(), sizes.end());
    if (sizes.size() >= 6 && sizes.size() <= 12 && largest <= 80) return PhoneScreen::Dashboard;   // nine dots
    if (sizes.size() <= 3 && largest >= 100) return PhoneScreen::Other;                             // one frame
    return PhoneScreen::Unknown;
}
}
