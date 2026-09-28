#pragma once
#include "androidauto/DisplayConfig.h"
#include <cstdint>
#include <utility>

namespace headunit {
// Where the phone's Android Auto currently is, as far as its picture tells.
enum class PhoneScreen {
    Unknown,    // no picture, a transition or an unfamiliar layout
    Dashboard,  // the home screen with the map, media and phone cards
    Other,      // any app, or the app launcher
};

// The button at the bottom left of Android Auto's navigation bar (position in the 800x480 layout). It switches between
// two symbols: nine dots (the launcher) while the dashboard is showing, and a framed split view (the dashboard)
// everywhere else, where tapping it goes to the dashboard. `KEYCODE_HOME` cannot be used for that: on current phones it
// always opens the app launcher.
inline constexpr int kDashboardButtonX = 42;
inline constexpr int kDashboardButtonY = 438;

// The layout is the same at every display size: the phone lays its interface out at a height of 480 units whatever the
// resolution (see DisplayDensity), so every position of the 800x480 layout is scaled by height/480 (in both directions)
// and stays anchored at the left and bottom edges of the shown area. (On a wide display the phone moves its navigation
// bar to a rail at the left; the button stays at the bottom left.)
constexpr double LayoutScale(int height)
{
    return height / 480.0;
}

std::pair<int, int> DashboardButtonPosition(const DisplayConfig& display);
PhoneScreen DetectPhoneScreen(const std::uint8_t* rgb, int width, int height, int stride);
}
