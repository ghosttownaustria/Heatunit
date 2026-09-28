#pragma once
#include "ui/MenuPage.h"
#include <QString>
#include <functional>
#include <optional>

class QTimer;

namespace headunit {
// The question a car asks when a phone pairs over Bluetooth: the phone's name and the six-digit code both show, and the
// person confirms on both. While it asks, the page is in front of everything else (the window's FrontPage), also of the
// phone's picture. Turning the knob or left / right choose between "Pair" and "Cancel", pushing it answers, Back
// cancels, a click on a button answers too. Without an answer it gives up after a minute (as the phone does).
class PairingPage final : public MenuPage {
public:
    explicit PairingPage(QWidget* parent = nullptr);

    void SetChangeHandler(std::function<void()> handler);
    void Ask(const QString& phone, const QString& code, std::function<void(bool isAccepted)> answer);
    void End();
    bool IsAsking() const;
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;
    bool Back() override;

protected:
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::function<void()> m_onChange;
    QString m_phone;
    QString m_code;
    std::function<void(bool)> m_answer;
    int m_focus{};   // 0: Pair, 1: Cancel
    std::optional<int> m_pressed;
    QTimer* m_timeout{};

    void Answer(bool isAccepted);
    QRectF ButtonRect(int index) const;
    std::optional<int> ButtonAt(const QPointF& position) const;
};
}
