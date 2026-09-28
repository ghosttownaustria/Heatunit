#include "ui/RotaryKnob.h"
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QToolTip>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace headunit {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDetentDegrees = 15.0;
constexpr int kWheelStep = 120;
const QColor kAccent(76, 141, 255);

// Degrees clockwise from 12 o'clock of `point` around `centre`.
double AngleAt(const QPointF& centre, const QPointF& point)
{
    return std::atan2(point.x() - centre.x(), centre.y() - point.y()) * 180.0 / kPi;
}

// The tooltip text of a place of the controller.
const char* ZoneTip(KnobZone zone)
{
    switch (zone) {
    case KnobZone::Up: return "Nach oben (Pfeil hoch)";
    case KnobZone::Right: return "Nach rechts (Pfeil rechts)";
    case KnobZone::Down: return "Nach unten (Pfeil runter)";
    case KnobZone::Left: return "Nach links (Pfeil links)";
    case KnobZone::Centre: return "Regler druecken: auswaehlen (Enter)";
    default: return "";
    }
}
}

// A knob of fixed size; the arrow under the mouse lights up.
RotaryKnob::RotaryKnob(QWidget* parent) : QWidget(parent)
{
    setFixedSize(kSize, kSize);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
}

// Who hears of turns: detents, +1 = clockwise.
void RotaryKnob::SetRotateHandler(std::function<void(int)> handler)
{
    m_onRotate = std::move(handler);
}

// Who hears of a click in the middle.
void RotaryKnob::SetPushHandler(std::function<void()> handler)
{
    m_onPush = std::move(handler);
}

// Who hears of a click on an arrow: its key code (DpadUp, ...).
void RotaryKnob::SetNudgeHandler(std::function<void(unsigned)> handler)
{
    m_onNudge = std::move(handler);
}

// The knob's fixed size.
QSize RotaryKnob::sizeHint() const
{
    return {kSize, kSize};
}

// One tooltip per place of the controller: the arrows, the middle and the turning itself.
bool RotaryKnob::event(QEvent* event)
{
    if (event->type() != QEvent::ToolTip) return QWidget::event(event);
    const auto* help = static_cast<QHelpEvent*>(event);
    const KnobZone zone = ZoneAt(help->pos());
    if (zone == KnobZone::None) QToolTip::hideText();
    else QToolTip::showText(help->globalPos(), QString::fromUtf8(ZoneTip(zone)) + "\nDrehen: Mausrad oder im Kreis ziehen", this);
    return true;
}

// The mouse left: nothing is lit any more.
void RotaryKnob::leaveEvent(QEvent* event)
{
    m_hoverZone = KnobZone::None;
    update();
    QWidget::leaveEvent(event);
}

// A press starts a click or a turn, whichever the mouse does next.
void RotaryKnob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_isPressed = true;
    m_isDragging = false;
    m_carry = 0;
    m_pressZone = ZoneAt(event->position());
    m_lastAngle = AngleAt(rect().center(), event->position());
    update();
}

// Without a press the arrow under the mouse lights up; with one the knob turns a detent every 15 degrees of dragging.
void RotaryKnob::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isPressed) {
        const KnobZone zone = ZoneAt(event->position());
        if (zone != m_hoverZone) {
            m_hoverZone = zone;
            update();
        }
        return;
    }
    const QPointF centre = rect().center();
    // Right at the middle a tiny movement is a huge turn: no turning there.
    if (std::hypot(event->position().x() - centre.x(), event->position().y() - centre.y()) < 10) return;
    const double now = AngleAt(centre, event->position());
    double delta = now - m_lastAngle;
    while (delta > 180) delta -= 360;
    while (delta < -180) delta += 360;
    m_lastAngle = now;
    m_carry += delta;
    while (m_carry >= kDetentDegrees) {
        m_carry -= kDetentDegrees;
        Detent(+1);
    }
    while (m_carry <= -kDetentDegrees) {
        m_carry += kDetentDegrees;
        Detent(-1);
    }
}

// A click that never turned the knob and ends where it began is a key: the arrow that was clicked or, in the middle,
// the controller's push button.
void RotaryKnob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_isPressed) return;
    m_isPressed = false;
    const KnobZone pressed = m_pressZone;
    m_pressZone = KnobZone::None;
    m_hoverZone = ZoneAt(event->position());
    if (!m_isDragging && pressed != KnobZone::None && pressed == m_hoverZone) {
        if (pressed == KnobZone::Centre) {
            if (m_onPush) m_onPush();
        } else if (m_onNudge) {
            m_onNudge(KnobZoneKey(pressed));
        }
    }
    update();
}

// Wheel up turns counter-clockwise (previous item), wheel down clockwise (next item).
void RotaryKnob::wheelEvent(QWheelEvent* event)
{
    m_wheelCarry += event->angleDelta().y();
    while (m_wheelCarry >= kWheelStep) {
        m_wheelCarry -= kWheelStep;
        Detent(-1);
    }
    while (m_wheelCarry <= -kWheelStep) {
        m_wheelCarry += kWheelStep;
        Detent(+1);
    }
    event->accept();
}

// Draws the disc, the lit arrow, the knurled rim, the arrows and the centre cap.
void RotaryKnob::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF centre = rect().center() + QPointF(0.5, 0.5);
    const double radius = std::min(width(), height()) / 2.0 - 4;
    const bool isDown = m_isPressed && !m_isDragging;   // a click that has not turned the knob so far
    QRadialGradient body(centre - QPointF(radius * 0.3, radius * 0.4), radius * 1.5);
    body.setColorAt(0, QColor(70, 77, 92));
    body.setColorAt(1, QColor(30, 34, 42));
    painter.setPen(QPen(QColor(150, 158, 174), 3));
    painter.setBrush(body);
    painter.drawEllipse(centre, radius, radius);
    const KnobZone lit = isDown ? m_pressZone : m_hoverZone;
    PaintLitSector(painter, centre, radius, lit, isDown);
    PaintKnurling(painter, centre, radius);
    PaintArrows(painter, centre, radius, lit, isDown);
    PaintCap(painter, centre, radius, isDown);
}

// One detent of turning in `direction`; a turn makes the current press a drag, not a click.
void RotaryKnob::Detent(int direction)
{
    m_angle += direction * kDetentDegrees;
    m_isDragging = true;
    if (m_onRotate) m_onRotate(direction);
    update();
}

// The zone at a widget position.
KnobZone RotaryKnob::ZoneAt(const QPointF& position) const
{
    const QPointF centre = rect().center() + QPointF(0.5, 0.5);
    return KnobZoneAt(position.x() - centre.x(), position.y() - centre.y(), std::min(width(), height()) / 2.0 - 4);
}

// The arrow under the mouse, or being clicked, lights up as a sector of the disc.
void RotaryKnob::PaintLitSector(QPainter& painter, const QPointF& centre, double radius, KnobZone lit, bool isDown) const
{
    if (lit == KnobZone::None || lit == KnobZone::Centre) return;
    // Degrees, counter-clockwise from 3 o'clock.
    const int start = lit == KnobZone::Up ? 45 : lit == KnobZone::Left ? 135 : lit == KnobZone::Down ? 225 : 315;
    const double inset = radius - 2;
    const double innerRadius = radius * kKnobCentreRatio;
    const QRectF outerBox(centre.x() - inset, centre.y() - inset, 2 * inset, 2 * inset);
    const QRectF innerBox(centre.x() - innerRadius, centre.y() - innerRadius, 2 * innerRadius, 2 * innerRadius);
    QPainterPath sector;
    sector.arcMoveTo(outerBox, start);
    sector.arcTo(outerBox, start, 90);
    sector.arcTo(innerBox, start + 90, -90);
    sector.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(isDown ? QColor(76, 141, 255, 110) : QColor(255, 255, 255, 26));
    painter.drawPath(sector);
}

// The knurled rim turns with the knob, so that the movement can be seen.
void RotaryKnob::PaintKnurling(QPainter& painter, const QPointF& centre, double radius) const
{
    painter.setPen(QPen(QColor(116, 124, 140), 2));
    for (int index = 0; index < 360 / static_cast<int>(kDetentDegrees); ++index) {
        const double angle = (m_angle + index * kDetentDegrees) * kPi / 180.0;
        const QPointF outer(centre.x() + std::sin(angle) * (radius - 4), centre.y() - std::cos(angle) * (radius - 4));
        const QPointF inner(centre.x() + std::sin(angle) * (radius - 10), centre.y() - std::cos(angle) * (radius - 10));
        painter.drawLine(inner, outer);
    }
}

// The four arrows, tips pointing to the rim.
void RotaryKnob::PaintArrows(QPainter& painter, const QPointF& centre, double radius, KnobZone lit, bool isDown) const
{
    const auto arrow = [&](KnobZone zone, double towardsX, double towardsY) {
        const double distance = radius * 0.74;
        const double reach = radius * 0.06;
        const double leg = radius * 0.11;
        const QColor colour = zone == lit ? (isDown ? kAccent.lighter(150) : QColor(255, 255, 255)) : QColor(214, 221, 233);
        painter.setPen(QPen(colour, 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        const QPointF tip(centre.x() + towardsX * (distance + reach), centre.y() + towardsY * (distance + reach));
        const QPointF base(centre.x() + towardsX * (distance - reach), centre.y() + towardsY * (distance - reach));
        const QPointF side(-towardsY, towardsX);
        painter.drawPolyline(QPolygonF({base + side * leg, tip, base - side * leg}));
    };
    arrow(KnobZone::Up, 0, -1);
    arrow(KnobZone::Right, 1, 0);
    arrow(KnobZone::Down, 0, 1);
    arrow(KnobZone::Left, -1, 0);
}

// The centre push button, with a marker that shows where the knob points.
void RotaryKnob::PaintCap(QPainter& painter, const QPointF& centre, double radius, bool isDown) const
{
    const double cap = radius * (kKnobCentreRatio - 0.06);
    painter.setPen(QPen(QColor(92, 100, 116), 2));
    const bool isCapHovered = m_hoverZone == KnobZone::Centre && !m_isPressed;
    painter.setBrush(isDown && m_pressZone == KnobZone::Centre ? kAccent : isCapHovered ? QColor(64, 72, 88) : QColor(46, 52, 64));
    painter.drawEllipse(centre, cap, cap);
    const double marker = m_angle * kPi / 180.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 196, 64));
    painter.drawEllipse(QPointF(centre.x() + std::sin(marker) * (cap - 11), centre.y() - std::cos(marker) * (cap - 11)), 5, 5);
}
}
