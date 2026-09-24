#include "ui/SettingsPage.h"
#include "ui/MenuStyle.h"
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// Layout in design units, like the player pages: the Settings tile at the left, the list in the column at its right.
const QRectF kTile(25, 100, 250, 400);
constexpr double kLeft = 300;
constexpr double kTitleBaseline = 132, kHintBaseline = 168;
constexpr double kListTop = 190, kRowHeight = 44;
constexpr double kCheckSize = 26, kCheckRight = 22;   // the tick box, from the row's right edge
}

SettingsPage::SettingsPage(QWidget* parent) : MenuPage(parent) {}

void SettingsPage::SetSetup(const HomeTileSetup& setup)
{
    m_setup = setup;
    m_focus = std::clamp(m_focus, 0, std::max(0, Count() - 1));
    update();
}
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
    if (onChange) onChange(setup);
}
void SettingsPage::Push() { Toggle(m_focus); }
void SettingsPage::Toggle(int index)
{
    if (index < 0 || index >= Count()) return;
    HomeTileSetup setup = m_setup;
    const auto& tile = setup.tiles[static_cast<std::size_t>(index)];
    if (SetTileShown(setup, tile.entry, !tile.isShown) && onChange) onChange(setup);
}
QRectF SettingsPage::RowRect(int index) const
{
    return QRectF(kLeft, kListTop + index * kRowHeight, Width() - kTile.left() - kLeft, kRowHeight);
}
std::optional<int> SettingsPage::RowAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    for (int i = 0; i < Count(); ++i)
        if (RowRect(i).contains(*point)) return i;
    return std::nullopt;
}
void SettingsPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kTile, HomeMenuEntry::Settings, true);
    const QFont titleFont = Font(30), hintFont = Font(22), noteFont = Font(20);
    const double width = Width() - kTile.left() - kLeft;
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kLeft, kTitleBaseline), Elided("Tiles on the home menu", titleFont, width));
    painter.setFont(hintFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kLeft, kHintBaseline), Elided("Turn: choose  ·  Push: show or hide  ·  Up / down: move", hintFont, width));
    for (int i = 0; i < Count(); ++i) {
        const auto& tile = m_setup.tiles[static_cast<std::size_t>(i)];
        const QRectF row = RowRect(i);
        DrawRow(painter, row, QString::fromLatin1(HomeMenuTitle(tile.entry)), QString(), i == m_focus, false);
        const QRectF check(row.right() - kCheckRight - kCheckSize, row.center().y() - kCheckSize / 2, kCheckSize, kCheckSize);
        DrawCheck(painter, check, tile.isShown);
        if (tile.entry == HomeMenuEntry::Settings) {
            painter.setFont(noteFont);
            painter.setPen(kDim);
            painter.drawText(QRectF(row.left(), row.top(), check.left() - 16 - row.left(), row.height()), Qt::AlignRight | Qt::AlignVCenter, "always shown");
        }
    }
}
void SettingsPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = RowAt(event->position());
}
void SettingsPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != RowAt(event->position())) return;
    m_focus = *pressed;
    Toggle(*pressed);
    update();
}
}
