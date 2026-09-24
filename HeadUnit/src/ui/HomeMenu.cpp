#include "ui/HomeMenu.h"
#include "ui/MenuStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <QVariantAnimation>
#include <utility>

namespace headunit {
HomeMenu::HomeMenu(QWidget* parent) : MenuPage(parent)
{
    m_scrollAnimation = new QVariantAnimation(this);
    m_scrollAnimation->setDuration(180);
    m_scrollAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_scrollAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) { m_scroll = value.toDouble(); update(); });
}
void HomeMenu::DisplayChanged()
{
    m_scrollAnimation->stop();
    m_scroll = m_targetScroll = HomeMenuScroll(m_focus, Width(), m_targetScroll);
}
void HomeMenu::Move(int steps) { Focus(MoveHomeFocus(m_focus, steps)); }
void HomeMenu::Activate() { if (onOpen) onOpen(Focused()); }
// The menu is one row: left and right move the focus, up and down have nothing to go to.
void HomeMenu::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadLeft) Move(-1);
    else if (keycode == keys::DpadRight) Move(+1);
}
void HomeMenu::Focus(int index)
{
    m_focus = index;
    const double target = HomeMenuScroll(index, Width(), m_targetScroll);
    if (target != m_targetScroll) {
        m_targetScroll = target;
        m_scrollAnimation->stop();
        m_scrollAnimation->setStartValue(m_scroll);
        m_scrollAnimation->setEndValue(target);
        m_scrollAnimation->start();
    }
    update();
}
std::optional<int> HomeMenu::TileAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    return HomeTileAt(point->x(), point->y(), m_scroll);
}
void HomeMenu::Paint(QPainter& painter)
{
    const double width = Width();
    const int lit = m_pressedTile.value_or(m_focus);
    for (int index = 0; index < kHomeMenuCount; ++index) {
        const QRectF tile(HomeTileLeft(index) - m_scroll, kHomeTileTop, kHomeTileWidth, kHomeTileHeight);
        if (tile.right() + menu::kFrameWidth < 0 || tile.left() - menu::kFrameWidth > width) continue;
        menu::DrawTile(painter, tile, kHomeMenuEntries[index], index == lit);
    }
}
void HomeMenu::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_pressedTile = TileAt(event->position());
    update();
}
void HomeMenu::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressedTile, std::nullopt);
    update();
    // Like a button: the tile opens when the click also ends on it. It keeps the focus afterwards.
    if (!pressed || pressed != TileAt(event->position())) return;
    Focus(*pressed);
    Activate();
}
}
