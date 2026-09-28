#include "ui/HomeMenu.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/HomeMenuLayout.h"
#include "ui/HomeTileSetup.h"
#include "ui/MenuStyle.h"
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// The edges and the bar, from the design.
constexpr double kEdgeTop = 75, kEdgeBottom = 525;
constexpr double kArrowTip = 44.3, kArrowBase = 55.7, kArrowY = 300, kArrowHalf = 13;   // from the screen's edge
constexpr int kWheelStep = 120;
constexpr int kScrollMilliseconds = 180;
const QColor kArrowColour(255, 91, 0);
const QColor kBarTrack(60, 60, 60);
}

// A menu with every tile, focused on the first; the row slides when the focus moves.
HomeMenu::HomeMenu(QWidget* parent) : MenuPage(parent), m_tiles(ShownTiles(DefaultTileSetup()))
{
    m_scrollAnimation = new QVariantAnimation(this);
    m_scrollAnimation->setDuration(kScrollMilliseconds);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_scrollAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        m_scroll = value.toDouble();
        update();
    });
}

// Who opens a tile.
void HomeMenu::SetOpenHandler(std::function<void(HomeMenuEntry)> handler)
{
    m_onOpen = std::move(handler);
}

// Who moves a tile one place along the row: -1 to the left, +1 to the right.
void HomeMenu::SetShiftHandler(std::function<void(HomeMenuEntry, int direction)> handler)
{
    m_onShift = std::move(handler);
}

// The tiles to show, in their order. The focus stays on the tile it was on while that tile is still shown.
void HomeMenu::SetTiles(std::vector<HomeMenuEntry> tiles)
{
    if (tiles.empty()) return;   // Settings is always there; an empty row is a caller's mistake
    const HomeMenuEntry focused = Focused();
    m_tiles = std::move(tiles);
    const auto found = std::find(m_tiles.begin(), m_tiles.end(), focused);
    Focus(found != m_tiles.end() ? static_cast<int>(found - m_tiles.begin()) : std::min(m_focus, Count() - 1));
}

// Moves the focus by `steps` tiles (positive: to the right); the row slides to keep it in the middle.
void HomeMenu::Move(int steps)
{
    Focus(MoveHomeFocus(m_focus, steps, Count()));
}

// Opens the focused tile.
void HomeMenu::Activate()
{
    if (m_onOpen) m_onOpen(Focused());
}

// The tile the focus is on.
HomeMenuEntry HomeMenu::Focused() const
{
    return m_tiles[static_cast<std::size_t>(m_focus)];
}

// Turning moves the focus.
void HomeMenu::Turn(int steps)
{
    Move(steps);
}

// Left and right move the focused tile itself; turning is what moves the focus. Up and down have nothing to do.
void HomeMenu::Nudge(unsigned keycode)
{
    if ((keycode == keys::DpadLeft || keycode == keys::DpadRight) && m_onShift) m_onShift(Focused(), keycode == keys::DpadLeft ? -1 : +1);
}

// Pushing opens the focused tile.
void HomeMenu::Push()
{
    Activate();
}

// Draws the visible tiles, the edges beyond which more follow and the bar (the whole row dark, the part in view light).
void HomeMenu::Paint(QPainter& painter)
{
    const double width = Width();
    const int lit = m_pressed && m_pressed->tile ? *m_pressed->tile : m_focus;
    for (int index = 0; index < Count(); ++index) {
        const QRectF tile(HomeTileLeft(index) - m_scroll, kHomeTileTop, kHomeTileWidth, kHomeTileHeight);
        if (tile.right() + menu::kFrameWidth < 0 || tile.left() - menu::kFrameWidth > width) continue;
        menu::DrawTile(painter, tile, m_tiles[static_cast<std::size_t>(index)], index == lit);
    }
    const HomeEdges edges = HomeMenuEdges(m_scroll, width, Count());
    if (edges.isLeft) DrawEdge(painter, false);
    if (edges.isRight) DrawEdge(painter, true);
    const HomeBar bar = HomeMenuBar(m_scroll, width, Count());
    painter.setPen(QPen(kBarTrack, 2.51, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(QPointF(kHomeTileMargin, kHomeBarY), QPointF(width - kHomeTileMargin, kHomeBarY));
    painter.setPen(QPen(menu::kText, 2.76, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(QPointF(bar.from, kHomeBarY), QPointF(bar.to, kHomeBarY));
}

// Another display width: the row jumps to where it belongs for the focus.
void HomeMenu::DisplayChanged()
{
    m_scrollAnimation->stop();
    m_scroll = m_targetScroll = HomeMenuScroll(m_focus, Width(), Count());
}

// A press on a tile or an edge arrow lights it until the release.
void HomeMenu::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const Spot spot = SpotAt(event->position());
    if (spot.tile || spot.edge != 0) m_pressed = spot;
    update();
}

// Like a button: it acts when the click also ends on it. A tile keeps the focus afterwards.
void HomeMenu::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    update();
    if (!pressed || *pressed != SpotAt(event->position())) return;
    if (pressed->edge != 0) {
        Move(pressed->edge);
        return;
    }
    Focus(*pressed->tile);
    Activate();
}

// The mouse wheel over the menu turns like the controller: one tile per notch.
void HomeMenu::wheelEvent(QWheelEvent* event)
{
    m_wheelCarry += event->angleDelta().y();
    while (m_wheelCarry >= kWheelStep) {
        m_wheelCarry -= kWheelStep;
        Move(-1);
    }
    while (m_wheelCarry <= -kWheelStep) {
        m_wheelCarry += kWheelStep;
        Move(+1);
    }
    event->accept();
}

// What is under a widget position. The edge strips cover the tiles behind them.
HomeMenu::Spot HomeMenu::SpotAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return {};
    const double width = Width();
    const HomeEdges edges = HomeMenuEdges(m_scroll, width, Count());
    if (point->y() >= kEdgeTop && point->y() < kEdgeBottom) {
        if (edges.isLeft && point->x() < kHomeEdgeWidth) return {std::nullopt, -1};
        if (edges.isRight && point->x() >= width - kHomeEdgeWidth) return {std::nullopt, +1};
    }
    return {HomeTileAt(point->x(), point->y(), m_scroll, Count()), 0};
}

// The number of tiles shown.
int HomeMenu::Count() const
{
    return static_cast<int>(m_tiles.size());
}

// Puts the focus on tile `index`; the row slides to its place for it.
void HomeMenu::Focus(int index)
{
    m_focus = std::clamp(index, 0, Count() - 1);
    const double target = HomeMenuScroll(m_focus, Width(), Count());
    if (target != m_targetScroll) {
        m_targetScroll = target;
        m_scrollAnimation->stop();
        m_scrollAnimation->setStartValue(m_scroll);
        m_scrollAnimation->setEndValue(target);
        m_scrollAnimation->start();
    }
    update();
}

// An edge beyond which more tiles follow, as the design draws it: the tiles fade into a black strip, a line marks it
// and an orange arrow points on.
void HomeMenu::DrawEdge(QPainter& painter, bool isRight) const
{
    const double width = Width();
    const double line = isRight ? width - kHomeEdgeWidth : kHomeEdgeWidth;
    const double inner = isRight ? line - kHomeEdgeFade : line + kHomeEdgeFade;
    QLinearGradient fade(QPointF(line, 0), QPointF(inner, 0));
    fade.setColorAt(0, QColor(0, 0, 0, 255));
    fade.setColorAt(0.65, QColor(0, 0, 0, 219));
    fade.setColorAt(1, QColor(0, 0, 0, 0));
    painter.fillRect(QRectF(QPointF(std::min(line, inner), kEdgeTop), QPointF(std::max(line, inner), kEdgeBottom)), fade);
    const QRectF strip = isRight ? QRectF(QPointF(line, kEdgeTop), QPointF(width + 1, kEdgeBottom)) : QRectF(QPointF(-1, kEdgeTop), QPointF(line, kEdgeBottom));
    painter.fillRect(strip, Qt::black);
    painter.setPen(QPen(menu::kText, 2.6, Qt::SolidLine, Qt::SquareCap));
    painter.drawLine(QPointF(line, kEdgeTop), QPointF(line, kEdgeBottom));
    const double tip = isRight ? width - kArrowTip : kArrowTip;
    const double base = isRight ? width - kArrowBase : kArrowBase;
    painter.setPen(QPen(kArrowColour, 4.2, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QPolygonF({QPointF(base, kArrowY - kArrowHalf), QPointF(tip, kArrowY), QPointF(base, kArrowY + kArrowHalf)}));
}
}
