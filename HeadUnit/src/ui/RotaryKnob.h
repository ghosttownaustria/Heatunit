#pragma once
#include "ui/KnobZones.h"
#include <QWidget>
#include <functional>

namespace headunit {
// The round controller of a car's centre console: a big disc with an arrow at each side (see KnobZones.h). Turn it with
// the mouse wheel or by dragging around it (one detent every 15 degrees); a click without dragging on an arrow at the
// rim is that direction key, a click in the middle presses the controller.
class RotaryKnob final : public QWidget {
public:
    static constexpr int kSize = 260;

    explicit RotaryKnob(QWidget* parent = nullptr);

    void SetRotateHandler(std::function<void(int)> handler);
    void SetPushHandler(std::function<void()> handler);
    void SetNudgeHandler(std::function<void(unsigned)> handler);
    QSize sizeHint() const override;

protected:
    bool event(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    std::function<void(int)> m_onRotate;         // detents, +1 = clockwise
    std::function<void()> m_onPush;              // the middle was clicked
    std::function<void(unsigned)> m_onNudge;     // an arrow was clicked: its key code (DpadUp, ...)
    double m_angle{0};                           // where the knob points, degrees clockwise from 12 o'clock
    double m_lastAngle{0};
    double m_carry{0};
    int m_wheelCarry{0};
    bool m_isPressed{};
    bool m_isDragging{};
    KnobZone m_hoverZone{KnobZone::None};
    KnobZone m_pressZone{KnobZone::None};

    void Detent(int direction);
    KnobZone ZoneAt(const QPointF& position) const;
    void PaintLitSector(QPainter& painter, const QPointF& centre, double radius, KnobZone lit, bool isDown) const;
    void PaintKnurling(QPainter& painter, const QPointF& centre, double radius) const;
    void PaintArrows(QPainter& painter, const QPointF& centre, double radius, KnobZone lit, bool isDown) const;
    void PaintCap(QPainter& painter, const QPointF& centre, double radius, bool isDown) const;
};
}
