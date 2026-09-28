#pragma once
#include "androidauto/ConsoleController.h"
#include "ui/AudioDisplay.h"
#include "ui/RotaryKnob.h"
#include <QString>
#include <QWidget>
#include <functional>

class QPushButton;
class QVBoxLayout;

namespace headunit {
// The simulated centre console. The top of it is the controller itself: MEDIA, TEL, NAV and the projection key (a phone
// with a play symbol) in a row, HOME and BACK below, and the round controller with its four arrows. Everything else
// (MENU, OPTION, RADIO, MAP, track skip, volume) sits in a separate section underneath, followed by the audio display.
class CarPanel final : public QWidget {
public:
    explicit CarPanel(QWidget* parent = nullptr);

    void SetConsoleHandler(std::function<void(ConsoleKey key)> handler);
    void SetKeyHandler(std::function<void(unsigned keycode, bool isDown)> handler);
    void SetRotateHandler(std::function<void(int detents)> handler);
    void SetVolumeHandler(std::function<void(int delta)> handler);
    void SetMuteHandler(std::function<void()> handler);
    AudioDisplay* Display();

private:
    std::function<void(ConsoleKey key)> m_onConsole;
    std::function<void(unsigned keycode, bool isDown)> m_onKey;
    std::function<void(int detents)> m_onRotate;
    std::function<void(int delta)> m_onVolume;
    std::function<void()> m_onMute;
    AudioDisplay* m_display{};
    RotaryKnob* m_knob{};

    void AddControllerKeys(QVBoxLayout* layout);
    void AddKnob(QVBoxLayout* layout);
    void AddMoreKeys(QVBoxLayout* layout);
    void AddVolumeKeys(QVBoxLayout* layout);
    void TapKey(unsigned keycode);
    QPushButton* AddKeyButton(const QString& text, unsigned keycode, const QString& tip);
    QPushButton* AddConsoleButton(const QString& text, ConsoleKey key, const QString& tip);
    QPushButton* AddPlainButton(const QString& text, const QString& tip);
};
}
