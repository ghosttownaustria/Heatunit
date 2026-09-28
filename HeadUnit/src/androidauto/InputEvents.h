#pragma once
#include <variant>

namespace headunit {
// The phase of a touch on the phone's screen.
enum class TouchAction { Down, Move, Up };

// One touch point, in touchscreen coordinates of the display announced to the phone.
struct TouchInput {
    TouchAction action{};
    int x{}, y{};
    unsigned pointerId{};
};

// A key press or release (see ProjectionKeys.h for the codes).
struct KeyInput {
    unsigned keycode{};
    bool isDown{};
};

// A turn of the rotary controller; positive is clockwise.
struct RotaryInput {
    int delta{};
};

using InputEvent = std::variant<TouchInput, KeyInput, RotaryInput>;
}
