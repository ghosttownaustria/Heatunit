#include "ui/BluetoothPage.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/MenuStyle.h"
#include "ui/PageFocus.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
// The list in the column right of the Bluetooth tile, as on the settings page.
constexpr double kHintBaseline = 168;
constexpr double kListTop = 190, kRowHeight = 50, kScrollbarRoom = 14;
constexpr int kVisibleRows = 6;
constexpr int kWheelStep = 120;
}

// A page without phones, as long as the Bluetooth service has not reported any.
BluetoothPage::BluetoothPage(QWidget* parent) : MenuPage(parent)
{
}

// Who makes a chosen phone the Android Auto phone.
void BluetoothPage::SetSwitchHandler(std::function<void(const BluetoothPhone&)> handler)
{
    m_onSwitch = std::move(handler);
}

// Whether this build has Bluetooth at all (wireless Android Auto, Linux only).
void BluetoothPage::SetAvailable(bool isAvailable)
{
    m_isAvailable = isAvailable;
    update();
}

// The phones as the Bluetooth service reports them. The focus stays on the phone it was on.
void BluetoothPage::SetPhones(std::vector<BluetoothPhone> phones)
{
    const std::string focused = m_focus < Count() ? m_phones[static_cast<std::size_t>(m_focus)].id : std::string();
    m_phones = SortedPhones(std::move(phones));
    const auto found = std::find_if(m_phones.begin(), m_phones.end(), [&focused](const BluetoothPhone& phone) { return phone.id == focused; });
    Focus(found != m_phones.end() ? static_cast<int>(found - m_phones.begin()) : m_focus);
}

// Turning moves through the list.
void BluetoothPage::Turn(int steps)
{
    Focus(m_focus + steps);
}

// Up and down move through the list like turning.
void BluetoothPage::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadUp) Focus(m_focus - 1);
    else if (keycode == keys::DpadDown) Focus(m_focus + 1);
}

// Pushing makes the focused phone the Android Auto phone.
void BluetoothPage::Push()
{
    Choose(m_focus);
}

// Draws the Bluetooth tile, the title with its hint and one row per phone.
void BluetoothPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kPageTile, HomeMenuEntry::Bluetooth, true);
    const QFont titleFont = Font(30);
    const QFont hintFont = Font(22);
    const double width = Width() - kPageTile.left() - kPageColumnLeft;
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kPageColumnLeft, kPageTitleBaseline), Elided("Paired phones", titleFont, width));
    painter.setFont(hintFont);
    painter.setPen(kDim);
    if (Count() == 0) {
        const QString empty = m_isAvailable ? "No phone is paired yet. Pair one with this head unit in the phone's Bluetooth settings."
                                            : "Bluetooth needs the Linux build (Raspberry Pi); this build has none.";
        painter.drawText(QRectF(kPageColumnLeft, kListTop - 36, width, kVisibleRows * kRowHeight), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, empty);
        return;
    }
    painter.drawText(QPointF(kPageColumnLeft, kHintBaseline), Elided(QString::fromUtf8("Turn: choose  ·  Push: use it for Android Auto"), hintFont, width));
    for (int index = 0; index < kVisibleRows && m_firstRow + index < Count(); ++index) {
        const int row = m_firstRow + index;
        const BluetoothPhone& phone = m_phones[static_cast<std::size_t>(row)];
        DrawRow(painter, RowRect(index), QString::fromStdString(phone.name), QString::fromStdString(PhoneStateText(phone)), row == m_focus, phone.isAndroidAuto);
    }
    if (Count() <= kVisibleRows) return;
    const double right = Width() - kPageTile.left();
    const QRectF track(right - 4, kListTop, 4, kVisibleRows * kRowHeight);
    painter.fillRect(track, kLine);
    const double thumb = std::max(24.0, track.height() * kVisibleRows / Count());
    const double top = track.top() + (track.height() - thumb) * m_firstRow / std::max(1, Count() - kVisibleRows);
    painter.fillRect(QRectF(track.left(), top, track.width(), thumb), kDim);
}

// Remembers the row a click starts on.
void BluetoothPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = RowAt(event->position());
}

// A click that ends on the row it started on focuses that phone and chooses it.
void BluetoothPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != RowAt(event->position())) return;
    Focus(*pressed);
    Choose(*pressed);
}

// The wheel scrolls the list without moving the focus.
void BluetoothPage::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / kWheelStep;
    m_firstRow = std::clamp(m_firstRow - steps, 0, std::max(0, Count() - kVisibleRows));
    update();
    event->accept();
}

// The number of phones listed.
int BluetoothPage::Count() const
{
    return static_cast<int>(m_phones.size());
}

// Puts the focus on `row` (kept in the list) and scrolls it into view.
void BluetoothPage::Focus(int row)
{
    m_focus = std::clamp(row, 0, std::max(0, Count() - 1));
    m_firstRow = ListFirstRow(m_focus, m_firstRow, kVisibleRows, Count());
    update();
}

// Where the `visibleIndex`th row in view is; a scrollbar takes room at the right when the list is longer than the view.
QRectF BluetoothPage::RowRect(int visibleIndex) const
{
    const double scrollbar = Count() > kVisibleRows ? kScrollbarRoom : 0;
    const double width = Width() - menu::kPageTile.left() - menu::kPageColumnLeft - scrollbar;
    return QRectF(menu::kPageColumnLeft, kListTop + visibleIndex * kRowHeight, width, kRowHeight);
}

// The row at a widget position.
std::optional<int> BluetoothPage::RowAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    for (int index = 0; index < kVisibleRows && m_firstRow + index < Count(); ++index) {
        if (RowRect(index).contains(*point)) return m_firstRow + index;
    }
    return std::nullopt;
}

// Hands phone `row` to the window, unless Android Auto runs on it already.
void BluetoothPage::Choose(int row)
{
    if (row < 0 || row >= Count() || !m_onSwitch) return;
    const BluetoothPhone& phone = m_phones[static_cast<std::size_t>(row)];
    if (CanSwitchTo(phone)) m_onSwitch(phone);
}
}
