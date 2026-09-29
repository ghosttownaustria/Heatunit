#pragma once
#include "androidauto/DisplayConfig.h"
#include "ui/StatusBar.h"
#include <QString>
#include <QWidget>
#include <functional>
#include <optional>

namespace headunit {
// A screen of the radio's own side (home menu, music player, tuner), shown in place of the phone's picture: a black
// screen of the chosen display's shape with the status bar at the top (StatusBar.h: the clock at the left; the sound's
// source, speaker, microphone and home at the right, which take touches before the page does). Pages paint in design
// units (600 high, see HomeMenuLayout.h). While a page is in front, the rotary controller works it instead of the phone.
class MenuPage : public QWidget {
public:
    explicit MenuPage(QWidget* parent = nullptr);

    void SetDisplay(const DisplayConfig& display);
    void SetStatus(const StatusState& status);
    void SetStatusHandler(std::function<void(StatusButton)> handler);
    // The controller turned: positive steps are clockwise.
    virtual void Turn(int steps) = 0;
    // An arrow of the controller (keys::Dpad*).
    virtual void Nudge(unsigned keycode) = 0;
    // The controller was pushed.
    virtual void Push() = 0;
    virtual bool Back();
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    // Draws the page on the black screen, in design units; the clock is already there.
    virtual void Paint(QPainter& painter) = 0;
    virtual void DisplayChanged();
    const DisplayConfig& Display() const;
    double Width() const;
    std::optional<QPointF> ToDesign(const QPointF& position) const;
    void paintEvent(QPaintEvent* event) final;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    DisplayConfig m_display{kDefaultDisplay};
    QString m_clock;
    StatusState m_status;
    std::function<void(StatusButton)> m_onStatus;
    std::optional<StatusButton> m_statusPressed;   // lit while a finger is on it

    QRectF ScreenRect() const;
    void UpdateClock();
    void PaintStatus(QPainter& painter) const;
    std::optional<StatusButton> StatusButtonUnder(const QPointF& position) const;
};
}
