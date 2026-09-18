#include "ui/CarWidgets.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace headunit {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kDetentDegrees = 15.0;
constexpr int kWheelStep = 120;
// Degrees clockwise from 12 o'clock of `point` around `centre`.
double AngleAt(const QPointF& centre, const QPointF& point) {
    return std::atan2(point.x() - centre.x(), centre.y() - point.y()) * 180.0 / kPi;
}
const char* const kPanelStyle =
    "QPushButton { background: #2b303a; color: #e8ecf2; border: 1px solid #4a5364; border-radius: 6px; padding: 6px 4px; }"
    "QPushButton:hover { background: #363d4a; }"
    "QPushButton:pressed { background: #4c8dff; border-color: #8ab4ff; color: white; }";
}

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
}
void VideoWidget::SetFrame(const VideoFrame& frame)
{
    // Copied because the frame's buffer belongs to the caller.
    m_image = QImage(frame.pixels.data(), frame.width, frame.height, frame.stride, QImage::Format_RGB888).copy();
    update();
}
void VideoWidget::ClearFrame(const QString& message)
{
    m_image = QImage();
    m_message = message;
    m_isTouching = false;
    update();
}
void VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));
    if (m_image.isNull()) {
        painter.setPen(QColor(221, 221, 221));
        QFont font = painter.font();
        font.setPointSize(14);
        painter.setFont(font);
        painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_message);
        return;
    }
    const QSize shown = m_image.size().scaled(size(), Qt::KeepAspectRatio);
    const QRect target(QPoint((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(target, m_image);
}
bool VideoWidget::Report(TouchAction action, const QPointF& position, bool isClamped)
{
    if (m_image.isNull() || !onTouch) return false;
    const auto point = MapToTouch(width(), height(), m_image.width(), m_image.height(), position.x(), position.y(), isClamped);
    if (!point) return false;
    onTouch(action, point->first, point->second);
    return true;
}
void VideoWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || m_isTouching) return;
    // Only a press on the picture starts a touch; the black bars around it do nothing.
    m_isTouching = Report(TouchAction::Down, event->position(), false);
}
void VideoWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Mice report far more often than a touchscreen; ~125 moves per second is plenty and the release
    // always carries the final position.
    if (!m_isTouching || (m_moveClock.isValid() && m_moveClock.elapsed() < 8)) return;
    m_moveClock.restart();
    Report(TouchAction::Move, event->position(), true);
}
void VideoWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_isTouching) return;
    m_isTouching = false;
    Report(TouchAction::Up, event->position(), true);
}

RotaryKnob::RotaryKnob(QWidget* parent) : QWidget(parent)
{
    setFixedSize(150, 150);
    setToolTip("Drehregler: Mausrad oder ziehen = drehen, klicken = druecken");
    setCursor(Qt::PointingHandCursor);
}
void RotaryKnob::Detent(int direction)
{
    m_angle += direction * kDetentDegrees;
    m_isDragging = true;
    if (onRotate) onRotate(direction);
    update();
}
void RotaryKnob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_isPressed = true;
    m_isDragging = false;
    m_carry = 0;
    m_lastAngle = AngleAt(rect().center(), event->position());
    update();
}
void RotaryKnob::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isPressed) return;
    const double now = AngleAt(rect().center(), event->position());
    double delta = now - m_lastAngle;
    while (delta > 180) delta -= 360;
    while (delta < -180) delta += 360;
    m_lastAngle = now;
    m_carry += delta;
    while (m_carry >= kDetentDegrees) { m_carry -= kDetentDegrees; Detent(+1); }
    while (m_carry <= -kDetentDegrees) { m_carry += kDetentDegrees; Detent(-1); }
}
void RotaryKnob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_isPressed) return;
    m_isPressed = false;
    // A click that never turned the knob is a press of its centre button.
    if (!m_isDragging && onPress) onPress();
    update();
}
void RotaryKnob::wheelEvent(QWheelEvent* event)
{
    m_wheelCarry += event->angleDelta().y();
    // Wheel up turns counter-clockwise (previous item), wheel down clockwise (next item).
    while (m_wheelCarry >= kWheelStep) { m_wheelCarry -= kWheelStep; Detent(-1); }
    while (m_wheelCarry <= -kWheelStep) { m_wheelCarry += kWheelStep; Detent(+1); }
    event->accept();
}
void RotaryKnob::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPointF centre = rect().center() + QPointF(0.5, 0.5);
    const double radius = std::min(width(), height()) / 2.0 - 4;
    QRadialGradient body(centre - QPointF(radius * 0.3, radius * 0.4), radius * 1.4);
    body.setColorAt(0, QColor(84, 92, 108));
    body.setColorAt(1, QColor(28, 31, 38));
    painter.setPen(QPen(QColor(110, 118, 134), 2));
    painter.setBrush(body);
    painter.drawEllipse(centre, radius, radius);
    // The knurled rim turns with the knob so the movement can be seen.
    painter.setPen(QPen(QColor(150, 158, 174), 2));
    for (int i = 0; i < 360 / static_cast<int>(kDetentDegrees); ++i) {
        const double angle = (m_angle + i * kDetentDegrees) * kPi / 180.0;
        const QPointF outer(centre.x() + std::sin(angle) * (radius - 2), centre.y() - std::cos(angle) * (radius - 2));
        const QPointF inner(centre.x() + std::sin(angle) * (radius - 9), centre.y() - std::cos(angle) * (radius - 9));
        painter.drawLine(inner, outer);
    }
    // Centre push button.
    const double cap = radius * 0.58;
    painter.setPen(QPen(QColor(90, 98, 114), 2));
    painter.setBrush(m_isPressed && !m_isDragging ? QColor(76, 141, 255) : QColor(52, 58, 70));
    painter.drawEllipse(centre, cap, cap);
    // Marker on the cap.
    const double marker = m_angle * kPi / 180.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 196, 64));
    painter.drawEllipse(QPointF(centre.x() + std::sin(marker) * (cap - 9), centre.y() - std::cos(marker) * (cap - 9)), 4.5, 4.5);
}

AudioDisplay::AudioDisplay(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(176);
    setMinimumWidth(240);
}
void AudioDisplay::SetState(int volume, bool isMuted, const std::array<AudioState::Meter, kAudioKindCount>& meters)
{
    m_volume = volume;
    m_isMuted = isMuted;
    for (int i = 0; i < kAudioKindCount; ++i) {
        // Peaks fall back slowly, like a real level meter.
        m_levels[i] = std::max(meters[i].peak, m_levels[i] * 0.82f);
        m_isActive[i] = meters[i].isActive;
    }
    update();
}
void AudioDisplay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor(74, 84, 100), 1));
    painter.setBrush(QColor(8, 14, 22));
    painter.drawRoundedRect(box, 8, 8);

    const QColor accent = m_isMuted ? QColor(90, 100, 112) : QColor(64, 208, 255);
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);
    painter.setPen(QColor(120, 134, 152));
    painter.drawText(QRectF(box.left() + 12, box.top() + 8, 120, 14), Qt::AlignLeft | Qt::AlignVCenter, "LAUTSTAERKE");
    if (m_isMuted) {
        painter.setPen(QColor(255, 96, 96));
        painter.drawText(QRectF(box.right() - 92, box.top() + 8, 80, 14), Qt::AlignRight | Qt::AlignVCenter, "STUMM");
    }
    // Volume number and segmented bar (one segment per step).
    QFont big = font;
    big.setPointSize(24);
    big.setBold(true);
    painter.setFont(big);
    painter.setPen(accent);
    painter.drawText(QRectF(box.left() + 12, box.top() + 22, 60, 38), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_volume));
    const double barLeft = box.left() + 74, barWidth = box.width() - 86;
    const double segment = barWidth / AudioState::kMaxVolume;
    painter.setPen(Qt::NoPen);
    for (int i = 0; i < AudioState::kMaxVolume; ++i) {
        painter.setBrush(i < m_volume ? accent : QColor(26, 36, 50));
        const double height = 8 + 20.0 * (i + 1) / AudioState::kMaxVolume;
        painter.drawRect(QRectF(barLeft + i * segment, box.top() + 56 - height, std::max(1.0, segment - 1.5), height));
    }

    static const char* const names[kAudioKindCount] = {"MEDIEN", "NAVI", "SYSTEM"};
    painter.setFont(font);
    for (int i = 0; i < kAudioKindCount; ++i) {
        const double top = box.top() + 72 + i * 32;
        painter.setPen(m_isActive[i] ? QColor(214, 224, 238) : QColor(112, 124, 140));
        painter.drawText(QRectF(box.left() + 12, top, 56, 20), Qt::AlignLeft | Qt::AlignVCenter, names[i]);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_isActive[i] ? QColor(72, 220, 110) : QColor(38, 48, 62));
        painter.drawEllipse(QPointF(box.left() + 76, top + 10), 4, 4);
        // 20 segments; the square root makes quiet audio visible.
        const double left = box.left() + 88, width = box.width() - 100;
        const double cell = width / 20;
        const int lit = static_cast<int>(std::lround(std::sqrt(std::clamp(m_levels[i], 0.0f, 1.0f)) * 20));
        for (int s = 0; s < 20; ++s) {
            const QColor on = s < 13 ? QColor(72, 220, 110) : s < 17 ? QColor(240, 200, 60) : QColor(240, 80, 70);
            painter.setBrush(s < lit ? on : QColor(24, 33, 46));
            painter.drawRect(QRectF(left + s * cell, top + 4, std::max(1.0, cell - 1.5), 12));
        }
    }
}

CarPanel::CarPanel(QWidget* parent) : QWidget(parent)
{
    setStyleSheet(kPanelStyle);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(8);
    m_display = new AudioDisplay(this);
    layout->addWidget(m_display);

    auto* volume = new QHBoxLayout();
    auto* down = AddPlainButton("Leiser  -", "Lautstaerke verringern (Taste -)");
    auto* mute = AddPlainButton("Stumm", "Ton aus/an (Taste M)");
    auto* up = AddPlainButton("Lauter  +", "Lautstaerke erhoehen (Taste +)");
    connect(down, &QPushButton::clicked, this, [this] { if (onVolume) onVolume(-1); });
    connect(up, &QPushButton::clicked, this, [this] { if (onVolume) onVolume(+1); });
    connect(mute, &QPushButton::clicked, this, [this] { if (onMute) onMute(); });
    volume->addWidget(down);
    volume->addWidget(mute);
    volume->addWidget(up);
    layout->addLayout(volume);

    // Knob with the four nudge keys around it, like a joystick-style controller.
    auto* pad = new QGridLayout();
    pad->setSpacing(4);
    m_knob = new RotaryKnob(this);
    m_knob->onRotate = [this](int detents) { if (onRotate) onRotate(detents); };
    m_knob->onPress = [this] { if (onKey) { onKey(keys::DpadCenter, true); onKey(keys::DpadCenter, false); } };
    const auto nudge = [&](const char* text, unsigned keycode, const char* tip, int row, int column) {
        auto* button = AddKeyButton(text, keycode, tip);
        button->setFixedSize(52, 36);
        button->setStyleSheet("font-size: 18px;");
        pad->addWidget(button, row, column, Qt::AlignCenter);
    };
    nudge("▲", keys::DpadUp, "Nach oben (Pfeil hoch)", 0, 1);
    nudge("◀", keys::DpadLeft, "Nach links (Pfeil links)", 1, 0);
    pad->addWidget(m_knob, 1, 1, Qt::AlignCenter);
    nudge("▶", keys::DpadRight, "Nach rechts (Pfeil rechts)", 1, 2);
    nudge("▼", keys::DpadDown, "Nach unten (Pfeil runter)", 2, 1);
    pad->setAlignment(Qt::AlignHCenter);
    layout->addLayout(pad);

    auto* hard = new QGridLayout();
    hard->setSpacing(6);
    hard->addWidget(AddKeyButton("Home", keys::Home, "Startbildschirm (Pos1)"), 0, 0);
    hard->addWidget(AddKeyButton("Zurueck", keys::Back, "Zurueck (Esc)"), 0, 1);
    hard->addWidget(AddKeyButton("Medien", keys::Media, "Medien-Taste"), 1, 0);
    hard->addWidget(AddKeyButton("Navi", keys::Navigation, "Navigations-Taste"), 1, 1);
    hard->addWidget(AddKeyButton("Telefon", keys::Tel, "Telefon-Taste"), 1, 2);
    hard->addWidget(AddKeyButton("◀◀ Titel", keys::MediaPrevious, "Voriger Titel (Bild hoch)"), 2, 0);
    hard->addWidget(AddKeyButton("Play/Pause", keys::MediaPlayPause, "Wiedergabe/Pause (Leertaste)"), 2, 1);
    hard->addWidget(AddKeyButton("Titel ▶▶", keys::MediaNext, "Naechster Titel (Bild runter)"), 2, 2);
    layout->addLayout(hard);
    layout->addStretch(1);
}
QPushButton* CarPanel::AddPlainButton(const QString& text, const QString& tip)
{
    auto* button = new QPushButton(text, this);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    return button;
}
QPushButton* CarPanel::AddKeyButton(const QString& text, unsigned keycode, const QString& tip)
{
    auto* button = AddPlainButton(QString::fromUtf8(text.toUtf8()), tip);
    // Down on press, up on release, so a held key looks like a held key to the phone.
    connect(button, &QPushButton::pressed, this, [this, keycode] { if (onKey) onKey(keycode, true); });
    connect(button, &QPushButton::released, this, [this, keycode] { if (onKey) onKey(keycode, false); });
    return button;
}
}
