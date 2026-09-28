#include "ui/SettingsPage.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/MenuStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// The list in the column right of the Settings tile.
constexpr double kHintBaseline = 168;
constexpr double kListTop = 190, kRowHeight = 44;
constexpr double kCheckSize = 26, kCheckRight = 22;   // the tick box, from the row's right edge
}

// A page with every tile, shown.
SettingsPage::SettingsPage(QWidget* parent) : MenuPage(parent)
{
}

// Who stores a change the user made; the window hands it back with SetSetup.
void SettingsPage::SetChangeHandler(std::function<void(const HomeTileSetup&)> handler)
{
    m_onChange = std::move(handler);
}

// The setup to show; the focus stays on its row where possible.
void SettingsPage::SetSetup(const HomeTileSetup& setup)
{
    m_setup = setup;
    m_focus = std::clamp(m_focus, 0, std::max(0, Count() - 1));
    update();
}

// Turning moves through the list.
void SettingsPage::Turn(int steps)
{
    m_focus = std::clamp(m_focus + steps, 0, std::max(0, Count() - 1));
    update();
}

// Up and down move the focused tile along the order; the focus goes with it.
void SettingsPage::Nudge(unsigned keycode)
{
    if (keycode != keys::DpadUp && keycode != keys::DpadDown) return;
    if (Count() == 0) return;
    HomeTileSetup setup = m_setup;
    const int direction = keycode == keys::DpadUp ? -1 : +1;
    if (!MoveTile(setup, setup.tiles[static_cast<std::size_t>(m_focus)].entry, direction, false)) return;
    m_focus += direction;
    if (m_onChange) m_onChange(setup);
}

// Pushing shows or hides the focused tile.
void SettingsPage::Push()
{
    Toggle(m_focus);
}

// Draws the Settings tile, the title with its hint and one row with a tick box per tile.
void SettingsPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kPageTile, HomeMenuEntry::Settings, true);
    const QFont titleFont = Font(30);
    const QFont hintFont = Font(22);
    const QFont noteFont = Font(20);
    const double width = Width() - kPageTile.left() - kPageColumnLeft;
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kPageColumnLeft, kPageTitleBaseline), Elided("Tiles on the home menu", titleFont, width));
    painter.setFont(hintFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kPageColumnLeft, kHintBaseline),
        Elided(QString::fromUtf8("Turn: choose  ·  Push: show or hide  ·  Up / down: move"), hintFont, width));
    for (int index = 0; index < Count(); ++index) {
        const auto& tile = m_setup.tiles[static_cast<std::size_t>(index)];
        const QRectF row = RowRect(index);
        DrawRow(painter, row, QString::fromLatin1(HomeMenuTitle(tile.entry)), QString(), index == m_focus, false);
        const QRectF check(row.right() - kCheckRight - kCheckSize, row.center().y() - kCheckSize / 2, kCheckSize, kCheckSize);
        DrawCheck(painter, check, tile.isShown);
        if (tile.entry != HomeMenuEntry::Settings) continue;
        painter.setFont(noteFont);
        painter.setPen(kDim);
        painter.drawText(QRectF(row.left(), row.top(), check.left() - 16 - row.left(), row.height()), Qt::AlignRight | Qt::AlignVCenter, "always shown");
    }
}

// Remembers the row a click starts on.
void SettingsPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = RowAt(event->position());
}

// A click that ends on the row it started on focuses and ticks it.
void SettingsPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != RowAt(event->position())) return;
    m_focus = *pressed;
    Toggle(*pressed);
    update();
}

// The number of tiles listed.
int SettingsPage::Count() const
{
    return static_cast<int>(m_setup.tiles.size());
}

// Where row `index` is, in design units.
QRectF SettingsPage::RowRect(int index) const
{
    return QRectF(menu::kPageColumnLeft, kListTop + index * kRowHeight, Width() - menu::kPageTile.left() - menu::kPageColumnLeft, kRowHeight);
}

// The row at a widget position.
std::optional<int> SettingsPage::RowAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    for (int index = 0; index < Count(); ++index) {
        if (RowRect(index).contains(*point)) return index;
    }
    return std::nullopt;
}

// Shows or hides tile `index`; a change goes to the window.
void SettingsPage::Toggle(int index)
{
    if (index < 0 || index >= Count()) return;
    HomeTileSetup setup = m_setup;
    const auto& tile = setup.tiles[static_cast<std::size_t>(index)];
    if (SetTileShown(setup, tile.entry, !tile.isShown) && m_onChange) m_onChange(setup);
}
}
