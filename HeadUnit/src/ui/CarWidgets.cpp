#include "ui/CarWidgets.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadialGradient>
#include <QToolTip>
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
// The projection key's symbol: a phone with a play triangle, drawn at twice the size so it stays sharp.
QIcon ProjectionIcon(const QColor& colour)
{
    constexpr int kLogical = 32;
    QPixmap pixmap(kLogical * 2, kLogical * 2);
    pixmap.setDevicePixelRatio(2.0);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPen pen(colour, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(6, 3, 15, 26), 3.2, 3.2);            // the phone
    painter.drawLine(QPointF(10.5, 24), QPointF(13.5, 24));              // its home bar
    // The play triangle overlaps the phone's edge: clear the edge under it first.
    const QPolygonF triangle({QPointF(17, 17), QPointF(28.5, 23), QPointF(17, 29)});
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.setPen(QPen(Qt::black, 6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::black);
    painter.drawPolygon(triangle);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolygon(triangle);
    painter.end();
    return QIcon(pixmap);
}
}

VideoWidget::VideoWidget(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMouseTracking(false);
    setCursor(Qt::PointingHandCursor);
}
void VideoWidget::SetFrame(const VideoFrame& frame)
{
    // Copied because the frame's buffer belongs to the caller. Only the shown area is kept: a display of
    // another shape than the phone's frame is fitted into it with black margins (see VideoLayout).
    const QImage whole(frame.pixels.data(), frame.width, frame.height, frame.stride, QImage::Format_RGB888);
    const VideoLayout layout = VideoLayoutOf(m_display);
    if (layout.HasMargins() && frame.width == layout.codecWidth && frame.height == layout.codecHeight)
        m_image = whole.copy(layout.Left(), layout.Top(), layout.width, layout.height);
    else
        m_image = whole.copy();
    update();
}
void VideoWidget::ClearFrame(const QString& message)
{
    m_image = QImage();
    m_message = message;
    m_isTouching = false;
    update();
}
void VideoWidget::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
    update();
}
void VideoWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));
    if (m_image.isNull()) {
        // An empty screen of the chosen display's shape, so its size can be judged before connecting.
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
        painter.drawText(screen.adjusted(0, 0, -12, -10), Qt::AlignRight | Qt::AlignBottom,
            QString("Display %1").arg(QString::fromStdString(DisplayText(m_display))));
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
    const auto point = MapToTouch(width(), height(), m_image.width(), m_image.height(), position.x(), position.y(), isClamped, m_display);
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
    setFixedSize(kSize, kSize);
    setMouseTracking(true);   // the arrow under the mouse lights up
    setCursor(Qt::PointingHandCursor);
}
namespace {
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
KnobZone RotaryKnob::ZoneAt(const QPointF& position) const
{
    const QPointF centre = rect().center() + QPointF(0.5, 0.5);
    return KnobZoneAt(position.x() - centre.x(), position.y() - centre.y(), std::min(width(), height()) / 2.0 - 4);
}
bool RotaryKnob::event(QEvent* event)
{
    // One tooltip per place of the controller: the arrows, the middle and the turning itself.
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const KnobZone zone = ZoneAt(help->pos());
        if (zone == KnobZone::None) QToolTip::hideText();
        else QToolTip::showText(help->globalPos(), QString::fromUtf8(ZoneTip(zone)) + "\nDrehen: Mausrad oder im Kreis ziehen", this);
        return true;
    }
    return QWidget::event(event);
}
void RotaryKnob::leaveEvent(QEvent* event)
{
    m_hoverZone = KnobZone::None;
    update();
    QWidget::leaveEvent(event);
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
    m_pressZone = ZoneAt(event->position());
    m_lastAngle = AngleAt(rect().center(), event->position());
    update();
}
void RotaryKnob::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_isPressed) {
        const KnobZone zone = ZoneAt(event->position());
        if (zone != m_hoverZone) { m_hoverZone = zone; update(); }
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
    while (m_carry >= kDetentDegrees) { m_carry -= kDetentDegrees; Detent(+1); }
    while (m_carry <= -kDetentDegrees) { m_carry += kDetentDegrees; Detent(-1); }
}
void RotaryKnob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton || !m_isPressed) return;
    m_isPressed = false;
    const KnobZone pressed = m_pressZone;
    m_pressZone = KnobZone::None;
    m_hoverZone = ZoneAt(event->position());
    // A click that never turned the knob and ends where it began is a key: the arrow that was clicked or,
    // in the middle, the controller's push button.
    if (!m_isDragging && pressed != KnobZone::None && pressed == m_hoverZone) {
        if (pressed == KnobZone::Centre) { if (onPress) onPress(); }
        else if (onNudge) onNudge(KnobZoneKey(pressed));
    }
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
    const QColor accent(76, 141, 255);
    const bool isDown = m_isPressed && !m_isDragging;   // a click that has not turned the knob so far
    // The disc with its rim.
    QRadialGradient body(centre - QPointF(radius * 0.3, radius * 0.4), radius * 1.5);
    body.setColorAt(0, QColor(70, 77, 92));
    body.setColorAt(1, QColor(30, 34, 42));
    painter.setPen(QPen(QColor(150, 158, 174), 3));
    painter.setBrush(body);
    painter.drawEllipse(centre, radius, radius);
    // The arrow under the mouse, or being clicked, lights up as a sector of the disc.
    const KnobZone lit = isDown ? m_pressZone : m_hoverZone;
    if (lit != KnobZone::None && lit != KnobZone::Centre) {
        const int start = lit == KnobZone::Up ? 45 : lit == KnobZone::Left ? 135 : lit == KnobZone::Down ? 225 : 315;   // degrees, counter-clockwise from 3 o'clock
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
    // The knurled rim turns with the knob so the movement can be seen.
    painter.setPen(QPen(QColor(116, 124, 140), 2));
    for (int i = 0; i < 360 / static_cast<int>(kDetentDegrees); ++i) {
        const double angle = (m_angle + i * kDetentDegrees) * kPi / 180.0;
        const QPointF outer(centre.x() + std::sin(angle) * (radius - 4), centre.y() - std::cos(angle) * (radius - 4));
        const QPointF inner(centre.x() + std::sin(angle) * (radius - 10), centre.y() - std::cos(angle) * (radius - 10));
        painter.drawLine(inner, outer);
    }
    // The four arrows, tips pointing to the rim.
    const auto arrow = [&](KnobZone zone, double towardsX, double towardsY) {
        const double distance = radius * 0.74, reach = radius * 0.06, leg = radius * 0.11;
        const QColor colour = zone == lit ? (isDown ? accent.lighter(150) : QColor(255, 255, 255)) : QColor(214, 221, 233);
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
    // Centre push button.
    const double cap = radius * (kKnobCentreRatio - 0.06);
    painter.setPen(QPen(QColor(92, 100, 116), 2));
    painter.setBrush(isDown && m_pressZone == KnobZone::Centre ? accent : (m_hoverZone == KnobZone::Centre && !m_isPressed) ? QColor(64, 72, 88) : QColor(46, 52, 64));
    painter.drawEllipse(centre, cap, cap);
    // Marker on the cap.
    const double marker = m_angle * kPi / 180.0;
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 196, 64));
    painter.drawEllipse(QPointF(centre.x() + std::sin(marker) * (cap - 11), centre.y() - std::cos(marker) * (cap - 11)), 5, 5);
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
    constexpr int kKeyHeight = 38;

    // The controller itself: MEDIA, TEL, NAV and the projection key in one row, HOME and BACK below,
    // then the round controller with its four arrows.
    auto* top = new QGridLayout();
    top->setSpacing(6);
    const auto hotKey = [&](const char* text, ConsoleKey key, const char* tip, int column) {
        auto* button = AddConsoleButton(text, key, tip);
        button->setFixedHeight(kKeyHeight);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        top->addWidget(button, 0, column);
        top->setColumnStretch(column, 1);
        return button;
    };
    hotKey("MEDIA", ConsoleKey::Media, "Medien (Spotify o.ae. auf dem Handy), Taste F3", 0);
    hotKey("TEL", ConsoleKey::Tel, "Telefon auf dem Handy, Taste F5", 1);
    hotKey("NAV", ConsoleKey::Nav, "Navigation auf dem Handy, Taste F6", 2);
    auto* projection = hotKey("", ConsoleKey::Projection, "CarPlay / Android Auto: Projektion nach vorn holen; ohne Verbindung: verbinden (Taste F8)", 3);
    projection->setIcon(ProjectionIcon(QColor(232, 236, 242)));
    projection->setIconSize(QSize(28, 28));
    layout->addLayout(top);

    auto* home = new QHBoxLayout();
    const auto sideKey = [&](const char* text, ConsoleKey key, const char* tip) {
        auto* button = AddConsoleButton(text, key, tip);
        button->setFixedSize(96, kKeyHeight);
        home->addWidget(button);
    };
    sideKey("HOME", ConsoleKey::Home, "Android-Auto-Startbildschirm; nochmal: Radio-Startmenue (Pos1)");
    home->addStretch(1);
    sideKey("BACK", ConsoleKey::Back, "Zurueck (Esc)");
    layout->addLayout(home);

    m_knob = new RotaryKnob(this);
    m_knob->onRotate = [this](int detents) { if (onRotate) onRotate(detents); };
    m_knob->onPress = [this] { if (onKey) { onKey(keys::DpadCenter, true); onKey(keys::DpadCenter, false); } };
    m_knob->onNudge = [this](unsigned keycode) { if (onKey) { onKey(keycode, true); onKey(keycode, false); } };
    layout->addWidget(m_knob, 0, Qt::AlignHCenter);

    // Everything else in a section of its own.
    auto* heading = new QHBoxLayout();
    const auto rule = [&] {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #3a4252;");
        return line;
    };
    auto* caption = new QLabel("WEITERE TASTEN", this);
    caption->setStyleSheet("color: #8590a5; font-size: 10px;");
    heading->addWidget(rule(), 1);
    heading->addWidget(caption);
    heading->addWidget(rule(), 1);
    layout->addLayout(heading);

    auto* more = new QGridLayout();
    more->setSpacing(6);
    more->addWidget(AddConsoleButton("MENU", ConsoleKey::Menu, "Radio-Startmenue, Taste F1"), 0, 0);
    more->addWidget(AddConsoleButton("OPTION", ConsoleKey::Option, "Optionen/Kontextmenue der aktuellen Ansicht (F2)"), 0, 1);
    more->addWidget(AddConsoleButton("RADIO", ConsoleKey::Radio, "Radio (noch ohne Funktion, zeigt das Radio-Startmenue), Taste F4"), 0, 2);
    more->addWidget(AddConsoleButton("MAP", ConsoleKey::Map, "Karte auf dem Handy, Taste F7"), 0, 3);
    layout->addLayout(more);

    // Track skip and play/pause go straight to the phone's media session.
    auto* skip = new QHBoxLayout();
    skip->setSpacing(6);
    skip->addWidget(AddKeyButton("◀◀ Titel", keys::MediaPrevious, "Voriger Titel (Bild hoch)"));
    skip->addWidget(AddKeyButton("Play/Pause", keys::MediaPlayPause, "Wiedergabe/Pause (Leertaste)"));
    skip->addWidget(AddKeyButton("Titel ▶▶", keys::MediaNext, "Naechster Titel (Bild runter)"));
    layout->addLayout(skip);

    auto* volume = new QHBoxLayout();
    volume->setSpacing(6);
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

    m_display = new AudioDisplay(this);
    layout->addWidget(m_display);
    layout->addStretch(1);
}
QPushButton* CarPanel::AddConsoleButton(const QString& text, ConsoleKey key, const QString& tip)
{
    auto* button = AddPlainButton(text, tip);
    // A controller key acts once per click; what it does depends on the console state.
    connect(button, &QPushButton::clicked, this, [this, key] { if (onConsole) onConsole(key); });
    return button;
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
