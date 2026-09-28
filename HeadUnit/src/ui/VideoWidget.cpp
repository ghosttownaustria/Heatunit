#include "ui/VideoWidget.h"
#include "androidauto/TouchMapping.h"
#include <QMouseEvent>
#include <QPainter>
#include <utility>

namespace headunit {
namespace {
// Mice report far more often than a touchscreen; about 125 moves per second is plenty, and the release always carries
// the final position.
constexpr qint64 kMoveIntervalMs = 8;
}

// An empty picture area that takes mouse input as touch.
VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
}

// Who gets the touches: position in touchscreen coordinates of the display (pixels of its shown area).
void VideoWidget::SetTouchHandler(std::function<void(TouchAction, int, int)> handler)
{
    m_onTouch = std::move(handler);
}

// Shows a decoded frame. It is copied because its buffer belongs to the caller. Only the shown area is kept: a display
// of another shape than the phone's frame is fitted into it with black margins (see VideoLayout).
void VideoWidget::SetFrame(const VideoFrame& frame)
{
    const QImage whole(frame.pixels.data(), frame.width, frame.height, frame.stride, QImage::Format_RGB888);
    const VideoLayout layout = VideoLayoutOf(m_display);
    if (layout.HasMargins() && frame.width == layout.codecWidth && frame.height == layout.codecHeight)
        m_image = whole.copy(layout.Left(), layout.Top(), layout.width, layout.height);
    else
        m_image = whole.copy();
    update();
}

// Removes the picture and shows `message` instead; a touch that was down ends.
void VideoWidget::ClearFrame(const QString& message)
{
    m_image = QImage();
    m_message = message;
    m_isTouching = false;
    update();
}

// Whether a picture of the phone is shown.
bool VideoWidget::HasFrame() const
{
    return !m_image.isNull();
}

// The picture of the phone (its shown area) as last drawn.
const QImage& VideoWidget::Image() const
{
    return m_image;
}

// The display the phone is given: its shown area is the touchscreen that mouse positions are mapped to (and the part of
// each frame that is kept), and without a picture the widget shows an empty screen of the same shape.
void VideoWidget::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
    update();
}

// The phone's smallest display.
QSize VideoWidget::sizeHint() const
{
    return {800, 480};
}

// Half the smallest display.
QSize VideoWidget::minimumSizeHint() const
{
    return {400, 240};
}

// Draws the picture as large as the widget allows, centred; without one an empty screen with the message.
void VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));
    if (m_image.isNull()) {
        PaintEmptyScreen(painter);
        return;
    }
    const QSize shown = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, m_image);
}

// Only a press on the picture starts a touch; the black bars around it do nothing.
void VideoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_isTouching) return;
    m_isTouching = Report(TouchAction::Down, event->position(), false);
}

// A finger that is down moves, paced to what a touchscreen reports.
void VideoWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isTouching || (m_moveClock.isValid() && m_moveClock.elapsed() < kMoveIntervalMs)) return;
    m_moveClock.restart();
    Report(TouchAction::Move, event->position(), true);
}

// The finger lifts, wherever the mouse is.
void VideoWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_isTouching) return;
    m_isTouching = false;
    Report(TouchAction::Up, event->position(), true);
}

// An empty screen of the chosen display's shape, so that its size can be judged before connecting.
void VideoWidget::PaintEmptyScreen(QPainter& painter)
{
    const QSize area = QSize(m_display.width, m_display.height).scaled(size() - QSize(24, 24), Qt::KeepAspectRatio);
    const QRect screen(QPoint((width() - area.width()) / 2, (height() - area.height()) / 2), area);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(70, 78, 92), 1));
    painter.setBrush(QColor(26, 29, 35));
    painter.drawRoundedRect(screen, 6, 6);
    painter.setPen(QColor(221, 221, 221));
    QFont font = painter.font();
    font.setPointSize(14);
    painter.setFont(font);
    painter.drawText(screen.adjusted(12, 12, -12, -34), Qt::AlignCenter | Qt::TextWordWrap, m_message);
    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(QColor(150, 158, 172));
    painter.drawText(screen.adjusted(0, 0, -12, -10), Qt::AlignRight | Qt::AlignBottom, QString("Display %1").arg(QString::fromStdString(DisplayText(m_display))));
}

// Hands a touch at `position` to the phone; false when it is outside the picture (unless `isClamped`) or nobody listens.
bool VideoWidget::Report(TouchAction action, const QPointF& position, bool isClamped)
{
    if (m_image.isNull() || !m_onTouch) return false;
    const auto point = MapToTouch(width(), height(), m_image.width(), m_image.height(), position.x(), position.y(), isClamped, m_display);
    if (!point) return false;
    m_onTouch(action, point->first, point->second);
    return true;
}
}
