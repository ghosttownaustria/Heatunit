#pragma once
#include "androidauto/DisplayConfig.h"
#include <QString>
#include <QWidget>
#include <optional>

namespace headunit {
// A screen of the radio's own side (home menu, music player, tuner), shown in place of the phone's picture: a black
// screen of the chosen display's shape with the clock at the top left. Pages paint in design units (600 high, see
// HomeMenuLayout.h). While a page is in front, the rotary controller works it instead of the phone.
class MenuPage : public QWidget {
public:
    explicit MenuPage(QWidget* parent = nullptr);
    void SetDisplay(const DisplayConfig& display);
    // The controller: turning (positive: clockwise), the four arrows (keys::Dpad*) and pushing it.
    virtual void Turn(int steps) = 0;
    virtual void Nudge(unsigned keycode) = 0;
    virtual void Push() = 0;
    // Back while the page is in front; true when the page took it itself (closing a list it opened).
    virtual bool Back() { return false; }
    QSize sizeHint() const override { return {800, 480}; }
    QSize minimumSizeHint() const override { return {400, 240}; }
protected:
    // Draws the page on the black screen, in design units; the clock is already there.
    virtual void Paint(QPainter& painter) = 0;
    virtual void DisplayChanged() {}
    const DisplayConfig& Display() const { return m_display; }
    // Width of the screen in design units.
    double Width() const;
    // A position in the widget as design units; none outside the screen.
    std::optional<QPointF> ToDesign(const QPointF& position) const;
    void paintEvent(QPaintEvent* event) final;
private:
    QRectF ScreenRect() const;
    void UpdateClock();
    DisplayConfig m_display{kDefaultDisplay};
    QString m_clock;
};
}
