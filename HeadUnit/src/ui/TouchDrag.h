#pragma once

namespace headunit {
// Tells a tap from a swipe on the touch screen (Qt hands a touch to the pages as mouse events): a press becomes a drag
// once the finger has moved more than kThreshold design units in any direction; from then on it scrolls or swipes and
// no longer counts as a click. The drag stays known after the release, so the release can ask what it was.
class TouchDrag {
public:
    static constexpr double kThreshold = 18;

    void Press(double x, double y);
    bool Move(double x, double y);
    void Release();
    bool IsDown() const;
    bool IsDragging() const;
    bool IsHorizontal() const;
    double DeltaX() const;
    double DeltaY() const;
    double StartX() const;
    double StartY() const;

private:
    double m_startX{}, m_startY{};
    double m_x{}, m_y{};
    bool m_isDown{};
    bool m_isDragging{};
};
}
