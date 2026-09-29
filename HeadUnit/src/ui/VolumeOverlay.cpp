#include "ui/VolumeOverlay.h"
#include "audio/AudioState.h"
#include "ui/HomeMenuLayout.h"
#include "ui/MenuStyle.h"
#include "ui/VolumeBar.h"
#include <QEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
constexpr int kShowMs = 2500;
// The segments grow from left to right, standing on one line near the bottom of the panel.
constexpr double kSegmentBaseline = 480, kSegmentLowest = 18, kSegmentHighest = 60, kSegmentGap = 4;
}

// A hidden bar over the whole of `parent` (the screens), following its size.
VolumeOverlay::VolumeOverlay(QWidget* parent) : QWidget(parent), m_hideTimer(new QTimer(this))
{
    m_hideTimer->setSingleShot(true);
    connect(m_hideTimer, &QTimer::timeout, this, [this] {
        if (!m_isDragging) hide();
    });
    parent->installEventFilter(this);
    setGeometry(parent->rect());
    hide();
}

// The display whose shape the screen has.
void VolumeOverlay::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
    UpdateMask();
    update();
}

// Who sets the volume that a touch on the bar chose.
void VolumeOverlay::SetVolumeHandler(std::function<void(int volume)> handler)
{
    m_onVolume = std::move(handler);
}

// Shows the bar with this volume, in front of everything, for a moment.
void VolumeOverlay::ShowVolume(int volume, bool isMuted)
{
    m_volume = volume;
    m_isMuted = isMuted;
    UpdateMask();
    show();
    raise();
    update();
    m_hideTimer->start(kShowMs);
}

// The screens changed their size: the bar covers them again.
bool VolumeOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent() && event->type() == QEvent::Resize) {
        setGeometry(parentWidget()->rect());
        UpdateMask();
    }
    return QWidget::eventFilter(watched, event);
}

// Draws the panel in design units: the tiles' frame with the focus corners, the speaker, the segments and the number.
void VolumeOverlay::paintEvent(QPaintEvent*)
{
    using namespace menu;
    const QRectF screen = ScreenRect();
    if (screen.isEmpty()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.translate(screen.topLeft());
    const double scale = screen.height() / kHomeMenuHeight;
    painter.scale(scale, scale);
    const QRectF panel = PanelRect();
    painter.fillRect(panel, QColor(0, 0, 0, 235));
    DrawFocus(painter, panel);
    DrawIcon(painter, Icon::Speaker, QRectF(panel.left() + 28, panel.top() + 30, kVolumeSymbolRoom - 50, panel.height() - 60), m_isMuted);

    const VolumePanel layout = VolumePanelOf(DesignWidth());
    const double cell = (layout.barRight - layout.barLeft) / AudioState::kMaxVolume;
    const QRectF bar(layout.barLeft, kSegmentBaseline - kSegmentHighest, layout.barRight - layout.barLeft, kSegmentHighest);
    const QBrush lit = m_isMuted ? QBrush(kDim) : LitBrush(bar);
    for (int step = 0; step < AudioState::kMaxVolume; ++step) {
        const double height = kSegmentLowest + (kSegmentHighest - kSegmentLowest) * step / (AudioState::kMaxVolume - 1);
        const QRectF segment(layout.barLeft + step * cell, kSegmentBaseline - height, std::max(1.0, cell - kSegmentGap), height);
        painter.fillRect(segment, step < m_volume ? lit : QBrush(kLine));
    }
    painter.setFont(Font(44));
    painter.setPen(m_isMuted ? kDim : kText);
    painter.drawText(QRectF(layout.barRight, panel.top(), panel.right() - layout.barRight - 24, panel.height()), Qt::AlignRight | Qt::AlignVCenter,
        QString::number(m_volume));
}

// A touch on the panel keeps the bar; on the segments it sets the volume and starts dragging.
void VolumeOverlay::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_isDragging = true;
    m_hideTimer->stop();
    SetVolumeAt(event->position());
}

// Dragging along the bar changes the volume as the finger moves.
void VolumeOverlay::mouseMoveEvent(QMouseEvent* event)
{
    if (m_isDragging) SetVolumeAt(event->position());
}

// The finger is lifted: the bar goes after the usual moment.
void VolumeOverlay::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_isDragging = false;
    m_hideTimer->start(kShowMs);
}

// Where the display is drawn in the widget, as the pages draw it.
QRectF VolumeOverlay::ScreenRect() const
{
    return menu::ScreenRectIn(QSizeF(size()), m_display);
}

// Width of the screen in design units.
double VolumeOverlay::DesignWidth() const
{
    return HomeMenuWidth(m_display);
}

// The panel in design units.
QRectF VolumeOverlay::PanelRect() const
{
    const VolumePanel layout = VolumePanelOf(DesignWidth());
    return QRectF(layout.left, kVolumePanelTop, layout.right - layout.left, kVolumePanelHeight);
}

// Only the panel is drawn and takes input; touches beside it reach the page or the phone's picture below.
void VolumeOverlay::UpdateMask()
{
    const QRectF screen = ScreenRect();
    if (screen.isEmpty()) return;
    const double scale = screen.height() / kHomeMenuHeight;
    const QRectF panel = PanelRect();
    const QRectF shown(screen.topLeft() + panel.topLeft() * scale, panel.size() * scale);
    setMask(QRegion(shown.toAlignedRect().adjusted(-2, -2, 2, 2)));
}

// The volume under a widget position, handed to the handler when it differs from the one shown.
void VolumeOverlay::SetVolumeAt(const QPointF& position)
{
    const QRectF screen = ScreenRect();
    if (screen.isEmpty()) return;
    const double x = (position.x() - screen.left()) / (screen.height() / kHomeMenuHeight);
    const int volume = VolumeAt(x, VolumePanelOf(DesignWidth()), AudioState::kMaxVolume);
    if (volume == m_volume || !m_onVolume) return;
    m_onVolume(volume);
}
}
