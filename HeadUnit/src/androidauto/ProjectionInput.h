#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <variant>

namespace headunit {
// The touchscreen the headunit advertises to the phone. The video is 800x480 as well, so
// video pixels and touch coordinates are the same thing.
constexpr int kTouchWidth = 800;
constexpr int kTouchHeight = 480;

// Android key codes the simulated car controls send. Values are Android's KeyEvent codes;
// the 0x10000 range are Android Auto's own car keys.
namespace keys {
constexpr unsigned Home = 3;
constexpr unsigned Back = 4;
constexpr unsigned Call = 5;
constexpr unsigned EndCall = 6;
constexpr unsigned DpadUp = 19;
constexpr unsigned DpadDown = 20;
constexpr unsigned DpadLeft = 21;
constexpr unsigned DpadRight = 22;
constexpr unsigned DpadCenter = 23;
constexpr unsigned Menu = 82;                // context menu ("Option")
constexpr unsigned Search = 84;              // voice assistant on most phones
constexpr unsigned MediaPlayPause = 85;
constexpr unsigned MediaStop = 86;
constexpr unsigned MediaNext = 87;
constexpr unsigned MediaPrevious = 88;
constexpr unsigned MediaPlay = 126;
constexpr unsigned MediaPause = 127;
constexpr unsigned RotaryController = 65536;  // relative event: delta = detents turned
constexpr unsigned Media = 65537;
constexpr unsigned Navigation = 65538;
constexpr unsigned Tel = 65540;
// What the phone may bind; anything else it asks for is answered but never sent.
constexpr unsigned Supported[] = {Home, Back, Call, EndCall, DpadUp, DpadDown, DpadLeft, DpadRight, DpadCenter, Menu, Search,
    MediaPlayPause, MediaStop, MediaNext, MediaPrevious, MediaPlay, MediaPause, RotaryController, Media, Navigation, Tel};
}

enum class TouchAction { Down, Move, Up };
struct TouchInput { TouchAction action{}; int x{}, y{}; unsigned pointerId{}; };
struct KeyInput { unsigned keycode{}; bool isDown{}; };
struct RotaryInput { int delta{}; };  // positive = clockwise
using InputEvent = std::variant<TouchInput, KeyInput, RotaryInput>;

// Carries input from the window (GUI thread) into a running Android Auto session (protocol
// thread). Events sent while no session is attached are dropped: the phone would ignore them
// anyway and a stale touch must never reach the next session.
class ProjectionInput {
public:
    using Sink = std::function<void(const InputEvent&)>;
    // Window side, any thread.
    void Touch(TouchAction action, int x, int y, unsigned pointerId = 0) { Send(TouchInput{action, x, y, pointerId}); }
    void Key(unsigned keycode, bool isDown) { Send(KeyInput{keycode, isDown}); }
    void Tap(unsigned keycode) { Key(keycode, true); Key(keycode, false); }
    void Rotate(int delta) { if (delta != 0) Send(RotaryInput{delta}); }
    bool IsAttached() const { std::lock_guard lock(m_mutex); return static_cast<bool>(m_sink); }
    // Session side. The sink runs under the lock, so once Detach returns it is never called again;
    // it must only hand the event over (post), never block.
    // Returns a token; only the holder of the current token can detach, so a finished session can never
    // detach its successor.
    std::uint64_t Attach(Sink sink) { std::lock_guard lock(m_mutex); m_sink = std::move(sink); return ++m_token; }
    void Detach(std::uint64_t token) { std::lock_guard lock(m_mutex); if (token == m_token) m_sink = nullptr; }
private:
    void Send(const InputEvent& event) {
        std::lock_guard lock(m_mutex);
        if (m_sink) m_sink(event);
    }
    mutable std::mutex m_mutex;
    Sink m_sink;
    std::uint64_t m_token{};
};

// Maps a position in a widget that shows the video letterboxed (aspect ratio kept, centered)
// to touchscreen coordinates. Outside the picture there is no position, unless `clamp` is set
// (a finger that is already down keeps reporting when it leaves the picture).
inline std::optional<std::pair<int, int>> MapToTouch(int widgetWidth, int widgetHeight, int videoWidth, int videoHeight,
    double x, double y, bool clamp)
{
    if (widgetWidth <= 0 || widgetHeight <= 0 || videoWidth <= 0 || videoHeight <= 0) return std::nullopt;
    const double scale = std::min(static_cast<double>(widgetWidth) / videoWidth, static_cast<double>(widgetHeight) / videoHeight);
    const double shownWidth = videoWidth * scale, shownHeight = videoHeight * scale;
    const double left = (widgetWidth - shownWidth) / 2, top = (widgetHeight - shownHeight) / 2;
    double u = (x - left) / shownWidth, v = (y - top) / shownHeight;
    if (!clamp && (u < 0 || u >= 1 || v < 0 || v >= 1)) return std::nullopt;
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const int touchX = std::min(kTouchWidth - 1, static_cast<int>(u * kTouchWidth));
    const int touchY = std::min(kTouchHeight - 1, static_cast<int>(v * kTouchHeight));
    return std::make_pair(touchX, touchY);
}
}
