#pragma once
#include <algorithm>
#include <optional>
#include <string>

namespace headunit {
// The status bar at the top of the radio's screens, after docs/design/heatunit.svg: the clock at the left (MenuPage),
// and at the right the source of the sound ("Jakob's Flip 8", a radio station), the speaker (mutes the sound), the
// microphone (mutes the microphone) and home (the Home key). Design units (the screen is 600 high and `width` wide,
// see HomeMenuLayout.h); the symbols keep their distance from the right edge on every display.
enum class StatusButton { Speaker, Microphone, Home };

// A rectangle in design units.
struct StatusBox {
    double left{}, top{}, width{}, height{};

    // Whether the point lies inside.
    constexpr bool Contains(double x, double y) const { return x >= left && x < left + width && y >= top && y < top + height; }
};

// What the status bar shows.
struct StatusState {
    friend bool operator==(const StatusState&, const StatusState&) = default;

    std::string source;          // where the sound comes from; empty: nothing to name
    bool isMuted{};
    bool isMicrophoneMuted{};
};

// The baseline of the source's name, as in the design.
inline constexpr double kStatusBaseline = 59.1;
// Touch areas reach this far down; the tiles start at 100.
inline constexpr double kStatusTouchTop = 8, kStatusTouchBottom = 92;

// Where a symbol is drawn, as in the design (1600 wide: speaker at 1401.9, microphone at 1472.0, home at 1532.5).
constexpr StatusBox StatusIconBox(StatusButton button, double width)
{
    switch (button) {
    case StatusButton::Speaker: return {width - 198.1, 37.9, 35.0, 24.2};
    case StatusButton::Microphone: return {width - 128.0, 32.5, 24.4, 35.0};
    case StatusButton::Home: return {width - 67.5, 32.5, 35.0, 35.0};
    }
    return {};
}

// The area a finger may hit for a symbol: wider and taller than the symbol, the three side by side without gaps.
constexpr StatusBox StatusTouchBox(StatusButton button, double width)
{
    switch (button) {
    case StatusButton::Speaker: return {width - 213, kStatusTouchTop, 65, kStatusTouchBottom - kStatusTouchTop};
    case StatusButton::Microphone: return {width - 148, kStatusTouchTop, 64, kStatusTouchBottom - kStatusTouchTop};
    case StatusButton::Home: return {width - 84, kStatusTouchTop, 68, kStatusTouchBottom - kStatusTouchTop};
    }
    return {};
}

// The symbol under a point, if any.
constexpr std::optional<StatusButton> StatusButtonAt(double x, double y, double width)
{
    for (const StatusButton button : {StatusButton::Speaker, StatusButton::Microphone, StatusButton::Home}) {
        if (StatusTouchBox(button, width).Contains(x, y)) return button;
    }
    return std::nullopt;
}

// The right end of the source's name, left of the speaker.
constexpr double StatusSourceRight(double width)
{
    return width - 225;
}

// How wide the source's name may get before it is shortened: a quarter of the screen, at most 400 units.
constexpr double StatusSourceMaxWidth(double width)
{
    return std::clamp(width / 4, 0.0, 400.0);
}

// Everything of the status bar lies right of this; buttons a page puts at the top must end before it.
constexpr double StatusBarLeft(double width)
{
    return StatusSourceRight(width) - StatusSourceMaxWidth(width);
}

// What the status bar names as the source of the sound: the radio's own player while it sounds (its station, its title),
// otherwise the projected phone (its name, or "Android Auto" while it has not told it), otherwise nothing.
inline std::string StatusSourceText(const std::string& localSource, bool isPhoneProjected, const std::string& phoneName)
{
    if (!localSource.empty()) return localSource;
    if (!isPhoneProjected) return {};
    return phoneName.empty() ? std::string("Android Auto") : phoneName;
}
}
