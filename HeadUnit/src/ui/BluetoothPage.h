#pragma once
#include "ui/BluetoothPhones.h"
#include "ui/MenuPage.h"
#include <functional>
#include <optional>
#include <vector>

namespace headunit {
// The phones paired with the head unit over Bluetooth: each with its state (Android Auto, connected, not connected),
// the Android Auto phone first and in orange. Choosing another phone (push, or a click on its row) makes it the Android
// Auto phone: the window ends the running session and has that phone connect anew, which starts Android Auto on it.
// Turning the knob or up / down move through the list. Without Bluetooth (the Windows build) the page says so.
class BluetoothPage final : public MenuPage {
public:
    explicit BluetoothPage(QWidget* parent = nullptr);

    void SetSwitchHandler(std::function<void(const BluetoothPhone&)> handler);
    void SetAvailable(bool isAvailable);
    void SetPhones(std::vector<BluetoothPhone> phones);
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;

protected:
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    std::function<void(const BluetoothPhone&)> m_onSwitch;
    std::vector<BluetoothPhone> m_phones;
    bool m_isAvailable{};
    int m_focus{};
    int m_firstRow{};
    std::optional<int> m_pressed;

    int Count() const;
    void Focus(int row);
    QRectF RowRect(int visibleIndex) const;
    std::optional<int> RowAt(const QPointF& position) const;
    void Choose(int row);
};
}
