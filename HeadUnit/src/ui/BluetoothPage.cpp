#include "ui/BluetoothPage.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/MenuStyle.h"
#include "ui/PageFocus.h"
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <utility>

namespace headunit {
namespace {
// The list in the column right of the Bluetooth tile, as on the settings page.
constexpr double kHintBaseline = 168;
constexpr double kListTop = 190, kRowHeight = 50, kScrollbarRoom = 14;
constexpr int kVisibleRows = 6;
constexpr int kWheelStep = 120;
constexpr double kButtonTop = 515, kButtonHeight = 44, kButtonGap = 20;   // the buttons below the list
constexpr double kConnectWidth = 190, kAutoConnectWidth = 300;

// Where the Connect button is, in design units.
QRectF ConnectRect()
{
    return QRectF(menu::kPageColumnLeft, kButtonTop, kConnectWidth, kButtonHeight);
}

// Where the Auto Connect button is, in design units.
QRectF AutoConnectRect()
{
    return QRectF(menu::kPageColumnLeft + kConnectWidth + kButtonGap, kButtonTop, kAutoConnectWidth, kButtonHeight);
}
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

// Who switches a phone between connecting by itself and connecting only on request; the window stores it.
void BluetoothPage::SetAutoConnectHandler(std::function<void(const BluetoothPhone&, bool)> handler)
{
    m_onAutoConnect = std::move(handler);
}

// Whether this build has Bluetooth at all (wireless Android Auto, Linux only).
void BluetoothPage::SetAvailable(bool isAvailable)
{
    m_isAvailable = isAvailable;
    update();
}

// The phones as the Bluetooth service reports them. The focus stays on the phone it was on, or on the button it was on.
void BluetoothPage::SetPhones(std::vector<BluetoothPhone> phones)
{
    const std::string selected = m_selected < Count() ? m_phones[static_cast<std::size_t>(m_selected)].id : std::string();
    const int button = Count() > 0 ? m_focus - Count() : -1;   // 0 or 1 while a button has the focus
    m_phones = SortedPhones(std::move(phones));
    const auto found = std::find_if(m_phones.begin(), m_phones.end(), [&selected](const BluetoothPhone& phone) { return phone.id == selected; });
    if (found != m_phones.end()) m_selected = static_cast<int>(found - m_phones.begin());
    Focus(button >= 0 && Count() > 0 ? ConnectIndex() + button : m_selected);
}

// Turning moves through the list and on to the buttons; back from the buttons it returns to the selected phone.
void BluetoothPage::Turn(int steps)
{
    const bool isBackToList = m_focus >= Count() && m_focus + steps < Count();
    Focus(isBackToList ? m_selected : m_focus + steps);
}

// Up and down move through the list like turning.
void BluetoothPage::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadUp) Turn(-1);
    else if (keycode == keys::DpadDown) Turn(+1);
}

// Pushing a phone moves on to the buttons; on a button it does what the button says.
void BluetoothPage::Push()
{
    Activate(m_focus);
}

// Draws the Bluetooth tile, the title with its hint, one row per phone and the two buttons.
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
    painter.drawText(QPointF(kPageColumnLeft, kHintBaseline), Elided(QString::fromUtf8("Turn: choose  ·  Push: to the buttons"), hintFont, width));
    for (int index = 0; index < kVisibleRows && m_firstRow + index < Count(); ++index) {
        const int row = m_firstRow + index;
        const BluetoothPhone& phone = m_phones[static_cast<std::size_t>(row)];
        DrawRow(painter, RowRect(index), QString::fromStdString(phone.name), QString::fromStdString(PhoneStateText(phone)), row == m_focus, phone.isAndroidAuto);
        if (row == m_selected && m_focus >= Count()) {
            painter.setPen(QPen(kFaintFrame, 2.5));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(RowRect(index));
        }
    }
    if (Count() > kVisibleRows) {
        const double right = Width() - kPageTile.left();
        const QRectF track(right - 4, kListTop, 4, kVisibleRows * kRowHeight);
        painter.fillRect(track, kLine);
        const double thumb = std::max(24.0, track.height() * kVisibleRows / Count());
        const double top = track.top() + (track.height() - thumb) * m_firstRow / std::max(1, Count() - kVisibleRows);
        painter.fillRect(QRectF(track.left(), top, track.width(), thumb), kDim);
    }
    const bool isAutoConnect = m_phones[static_cast<std::size_t>(m_selected)].isAutoConnect;
    DrawTextButton(painter, ConnectRect(), "Connect", m_focus == ConnectIndex());
    DrawTextButton(painter, AutoConnectRect(), isAutoConnect ? "Auto Connect: On" : "Auto Connect: Off", m_focus == AutoConnectIndex());
}

// Remembers the row or button a click starts on; it may also become a drag.
void BluetoothPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_pressed = TargetAt(event->position());
    if (const auto point = ToDesign(event->position())) {
        m_drag.Press(point->x(), point->y());
        m_dragFirstRow = m_firstRow;
    }
}

// Dragging up or down scrolls the list with the finger.
void BluetoothPage::mouseMoveEvent(QMouseEvent* event)
{
    const auto point = ToDesign(event->position());
    if (!point || !m_drag.Move(point->x(), point->y())) return;
    m_pressed.reset();
    const int rows = static_cast<int>(std::lround(m_drag.DeltaY() / kRowHeight));
    m_firstRow = std::clamp(m_dragFirstRow - rows, 0, std::max(0, Count() - kVisibleRows));
    update();
}

// A click that ends on the row or button it started on selects that phone or presses that button; a drag does nothing.
void BluetoothPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    m_drag.Release();
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != TargetAt(event->position())) return;
    Focus(*pressed);
    if (*pressed >= Count()) Activate(*pressed);
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

// The focus index of the Connect button: the one after the last phone.
int BluetoothPage::ConnectIndex() const
{
    return Count();
}

// The focus index of the Auto Connect button.
int BluetoothPage::AutoConnectIndex() const
{
    return Count() + 1;
}

// Puts the focus on `index` (kept in the list and the buttons); a phone's row becomes the selected phone and is scrolled
// into view. Without phones there is nothing to focus.
void BluetoothPage::Focus(int index)
{
    m_focus = std::clamp(index, 0, Count() > 0 ? AutoConnectIndex() : 0);
    m_selected = std::clamp(m_focus < Count() ? m_focus : m_selected, 0, std::max(0, Count() - 1));
    m_firstRow = ListFirstRow(m_selected, m_firstRow, kVisibleRows, Count());
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

// The row or button (ConnectIndex, AutoConnectIndex) at a widget position.
std::optional<int> BluetoothPage::TargetAt(const QPointF& position) const
{
    if (Count() > 0) {
        if (const auto point = ToDesign(position)) {
            if (ConnectRect().contains(*point)) return ConnectIndex();
            if (AutoConnectRect().contains(*point)) return AutoConnectIndex();
        }
    }
    return RowAt(position);
}

// Presses what has the focus: a phone's row moves on to the buttons, a button acts on the selected phone.
void BluetoothPage::Activate(int index)
{
    if (index < Count()) Focus(ConnectIndex());
    else if (index == ConnectIndex()) Choose(m_selected);
    else if (index == AutoConnectIndex()) ToggleAutoConnect(m_selected);
}

// Hands phone `row` to the window, unless Android Auto runs on it already.
void BluetoothPage::Choose(int row)
{
    if (row < 0 || row >= Count() || !m_onSwitch) return;
    const BluetoothPhone& phone = m_phones[static_cast<std::size_t>(row)];
    if (CanSwitchTo(phone)) m_onSwitch(phone);
}

// Switches phone `row` between connecting by itself and connecting on request; it shows at once, the window stores it.
void BluetoothPage::ToggleAutoConnect(int row)
{
    if (row < 0 || row >= Count()) return;
    BluetoothPhone& phone = m_phones[static_cast<std::size_t>(row)];
    phone.isAutoConnect = !phone.isAutoConnect;
    if (m_onAutoConnect) m_onAutoConnect(phone, phone.isAutoConnect);
    update();
}
}
