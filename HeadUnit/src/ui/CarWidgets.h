#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include "video/VideoDecoder.h"
#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QWidget>
#include <array>
#include <functional>

class QPushButton;

namespace headunit {
// Shows the phone's picture (aspect ratio kept) and turns mouse input on it into touch events.
class VideoWidget final : public QWidget {
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void SetFrame(const VideoFrame& frame);
    // Removes the picture and shows `message` instead.
    void ClearFrame(const QString& message);
    bool HasFrame() const { return !m_image.isNull(); }
    const QImage& Image() const { return m_image; }
    // The display the phone is given: its shown area is the touchscreen that mouse positions are mapped to
    // (and the part of each frame that is kept), and without a picture the widget shows an empty screen of
    // the same shape.
    void SetDisplay(const DisplayConfig& display);
    // Position in touchscreen coordinates of the display (pixels of its shown area).
    std::function<void(TouchAction, int, int)> onTouch;
    QSize sizeHint() const override { return {800, 480}; }
    QSize minimumSizeHint() const override { return {400, 240}; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    bool Report(TouchAction action, const QPointF& position, bool isClamped);
    QImage m_image;
    QString m_message;
    DisplayConfig m_display{kDefaultDisplay};
    bool m_isTouching{};
    QElapsedTimer m_moveClock;   // paces move events while a finger is down
};

// A rotary controller like a car's centre console knob: turn it with the mouse wheel or by
// dragging around it (one detent every 15 degrees); a click without dragging presses it.
class RotaryKnob final : public QWidget {
public:
    explicit RotaryKnob(QWidget* parent = nullptr);
    std::function<void(int)> onRotate;   // detents, +1 = clockwise
    std::function<void()> onPress;
    QSize sizeHint() const override { return {150, 150}; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    void Detent(int direction);
    double m_angle{0};        // where the knob points, degrees clockwise from 12 o'clock
    double m_lastAngle{0}, m_carry{0};
    int m_wheelCarry{0};
    bool m_isPressed{}, m_isDragging{};
    bool m_isHover{};
};

// The car's audio readout: volume steps, mute and a level meter for each of the phone's streams.
class AudioDisplay final : public QWidget {
public:
    explicit AudioDisplay(QWidget* parent = nullptr);
    void SetState(int volume, bool isMuted, const std::array<AudioState::Meter, kAudioKindCount>& meters);
    QSize sizeHint() const override { return {260, 176}; }
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    int m_volume{};
    bool m_isMuted{};
    std::array<float, kAudioKindCount> m_levels{};
    std::array<bool, kAudioKindCount> m_isActive{};
};

// The simulated centre console, laid out like a BMW iDrive multimedia controller: audio display with volume keys, the rotary knob with its four
// nudge keys and MENU/HOME/BACK/OPTION around it, the MEDIA/RADIO/TEL/NAV/MAP hot keys, the projection key and track skip.
class CarPanel final : public QWidget {
public:
    explicit CarPanel(QWidget* parent = nullptr);
    // Controller keys (Home, Menu, Media, ...); what they mean is decided by the ConsoleController.
    std::function<void(ConsoleKey key)> onConsole;
    // Keys that go straight to the phone: nudge keys and the track/play keys (down on press, up on release).
    std::function<void(unsigned keycode, bool isDown)> onKey;
    std::function<void(int detents)> onRotate;
    std::function<void(int delta)> onVolume;
    std::function<void()> onMute;
    AudioDisplay* Display() { return m_display; }
private:
    QPushButton* AddKeyButton(const QString& text, unsigned keycode, const QString& tip);
    QPushButton* AddConsoleButton(const QString& text, ConsoleKey key, const QString& tip);
    QPushButton* AddPlainButton(const QString& text, const QString& tip);
    AudioDisplay* m_display{};
    RotaryKnob* m_knob{};
};
}
