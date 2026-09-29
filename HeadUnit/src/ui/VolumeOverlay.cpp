#include "ui/VolumeOverlay.h"
#include "audio/AudioState.h"
#include "ui/HomeMenuLayout.h"
#include "ui/MenuStyle.h"
#include "ui/VolumeBar.h"
#include <QEvent>
#include <QMouseEvent>
#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRegion>
#include <QSizeF>
#include <QTimer>
#include <utility>

namespace headunit {
namespace {
constexpr int kShowMs = 2500;
constexpr double kSpeakerLeft = 29, kLineWidth = 4.84;
constexpr QSizeF kSpeakerSize{17, 24};
// Layers of the shadow: each adds a little black, so it fades out to the sides; it hangs slightly lower than the panel.
constexpr int kShadowAlpha = 5;
constexpr double kShadowDrop = 4;

// A soft black shadow around `panel`, so the panel stands out from what is behind it.
void DrawShadow(QPainter& painter, const QRectF& panel)
{
    for (int grow = static_cast<int>(headunit::kVolumeShadowMargin); grow > 0; --grow) {
        painter.fillRect(panel.adjusted(-grow, -grow, grow, grow).translated(0, kShadowDrop), QColor(0, 0, 0, kShadowAlpha));
    }
}
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

// Draws the panel in design units: shadow, shaded face, the speaker and the line lit up to the volume.
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
    DrawShadow(painter, panel);
    QLinearGradient face(panel.bottomLeft(), panel.topLeft());
    face.setColorAt(0, QColor(26, 26, 26));
    face.setColorAt(1, QColor(88, 88, 88));
    painter.fillRect(panel, face);
    DrawIcon(painter, Icon::Speaker, QRectF(panel.left() + kSpeakerLeft, panel.center().y() - kSpeakerSize.height() / 2, kSpeakerSize.width(),
        kSpeakerSize.height()), m_isMuted);

    const VolumePanel layout = VolumePanelOf(DesignWidth());
    const double y = panel.center().y();
    const double litRight = layout.barLeft + (layout.barRight - layout.barLeft) * m_volume / AudioState::kMaxVolume;
    painter.setPen(QPen(kText, kLineWidth, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(QPointF(layout.barLeft, y), QPointF(layout.barRight, y));
    if (m_volume <= 0) return;
    painter.setPen(QPen(m_isMuted ? kDim : QColor(255, 45, 0), kLineWidth + 1, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(QPointF(layout.barLeft, y), QPointF(litRight, y));
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

// Only the panel and its shadow are drawn and take input; touches beside them reach the page or the phone's picture below.
void VolumeOverlay::UpdateMask()
{
    const QRectF screen = ScreenRect();
    if (screen.isEmpty()) return;
    const double scale = screen.height() / kHomeMenuHeight;
    const double margin = kVolumeShadowMargin + kShadowDrop;
    const QRectF panel = PanelRect().adjusted(-margin, -margin, margin, margin);
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
