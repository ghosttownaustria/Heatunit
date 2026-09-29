#pragma once
#include "ui/BluetoothPhones.h"
#include "ui/MenuPage.h"
#include "ui/TouchDrag.h"
#include <functional>
#include <optional>
#include <vector>

namespace headunit {
// The phones paired with the head unit over Bluetooth: each with its state (Android Auto, connected, not connected),
// the Android Auto phone first and in orange. A tap or the knob selects a phone; below the list, two buttons act on it.
// Connect makes it the Android Auto phone: the window ends the running session and has that phone connect anew, which
// starts Android Auto on it. Auto Connect switches whether the head unit connects the phone by itself (on) or only when
// Connect is used (off). Turning the knob or up / down move through the list and on to the buttons (pushing a phone
// jumps to them); dragging on the list scrolls it. Without Bluetooth (the Windows build) the page says so.
class BluetoothPage final : public MenuPage {
public:
    explicit BluetoothPage(QWidget* parent = nullptr);

    void SetSwitchHandler(std::function<void(const BluetoothPhone&)> handler);
    void SetAutoConnectHandler(std::function<void(const BluetoothPhone&, bool isAutoConnect)> handler);
    void SetAvailable(bool isAvailable);
    void SetPhones(std::vector<BluetoothPhone> phones);
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;

protected:
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    std::function<void(const BluetoothPhone&)> m_onSwitch;
    std::function<void(const BluetoothPhone&, bool)> m_onAutoConnect;
    std::vector<BluetoothPhone> m_phones;
    bool m_isAvailable{};
    int m_focus{};      // a phone's row, then ConnectIndex(), then AutoConnectIndex()
    int m_selected{};   // the phone the buttons act on: the row that had the focus last
    int m_firstRow{};
    std::optional<int> m_pressed;   // the row or button a click started on
    TouchDrag m_drag;
    int m_dragFirstRow{};   // the list's scroll when the finger went down

    int Count() const;
    int ConnectIndex() const;
    int AutoConnectIndex() const;
    void Focus(int index);
    QRectF RowRect(int visibleIndex) const;
    std::optional<int> RowAt(const QPointF& position) const;
    std::optional<int> TargetAt(const QPointF& position) const;
    void Activate(int index);
    void Choose(int row);
    void ToggleAutoConnect(int row);
};
}
