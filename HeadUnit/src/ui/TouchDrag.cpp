#include "ui/TouchDrag.h"
#include <cmath>

namespace headunit {
// A finger (or the mouse button) goes down at `x`,`y`: a new tap, until it moves far enough.
void TouchDrag::Press(double x, double y)
{
    m_startX = m_x = x;
    m_startY = m_y = y;
    m_isDown = true;
    m_isDragging = false;
}

// The finger moved to `x`,`y`; true once the press is a drag. Ignored while nothing is pressed.
bool TouchDrag::Move(double x, double y)
{
    if (!m_isDown) return false;
    m_x = x;
    m_y = y;
    if (!m_isDragging && (std::abs(DeltaX()) > kThreshold || std::abs(DeltaY()) > kThreshold)) m_isDragging = true;
    return m_isDragging;
}

// The finger is lifted; whether it was a drag stays known until the next press.
void TouchDrag::Release()
{
    m_isDown = false;
}

// Whether a finger is down.
bool TouchDrag::IsDown() const
{
    return m_isDown;
}

// Whether the press has become a drag (and so is no click).
bool TouchDrag::IsDragging() const
{
    return m_isDragging;
}

// Whether the drag goes more sideways than up or down.
bool TouchDrag::IsHorizontal() const
{
    return std::abs(DeltaX()) > std::abs(DeltaY());
}

// How far the finger moved to the right since the press (negative: to the left).
double TouchDrag::DeltaX() const
{
    return m_x - m_startX;
}

// How far the finger moved down since the press (negative: up).
double TouchDrag::DeltaY() const
{
    return m_y - m_startY;
}

// Where the press started.
double TouchDrag::StartX() const
{
    return m_startX;
}

// Where the press started.
double TouchDrag::StartY() const
{
    return m_startY;
}
}
