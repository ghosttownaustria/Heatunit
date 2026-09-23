#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioEngine.h"
#include "audio/AudioTypes.h"
#include "usb/AutoConnectSystem.h"
#include "usb/IUsbBackend.h"
#include "logging/Logger.h"
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
class QCloseEvent;
class QComboBox;
class QKeyEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace headunit {
class CarPanel;
class VideoWidget;

// One button connects (finding the phone, repairing the driver, starting Android Auto, restarting the
// USB link when needed) and, while running, ends the session again. Next to the picture sits the
// simulated centre console: rotary knob, hard keys and the audio display. The picture itself takes
// mouse input as touch. The display size (video resolution) is chosen next to the button and only
// while nothing is connected: the phone is told the size once, when the connection starts.
class MainWindow final : public QMainWindow {
public:
    // The test modes run one scripted check against the real phone and exit with its result.
    enum class TestMode { None, Smoke, Projection, Input, Audio, Console, Keys };
    MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode = TestMode::None);
    ~MainWindow() override;
    // Selects the display size for the next connection (ignored while a connection is running or for a
    // size that is not offered). Not remembered: only a choice made in the window is.
    void SetDisplay(const DisplayConfig& display);
    // Starts "Android Auto kabellos" (Linux): Wi-Fi hotspot plus Bluetooth, no cable. Does nothing where that is not built.
    void StartWirelessConnect();
protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
private:
    enum class State { Idle, Connecting, Stopping };
    void SetState(State state);
    void OnButton();
    void StartConnect();
    void BeginConnect(bool isWireless);
    void RequestStop();
    void FinishConnect(const AutoConnectResult& result);
    void ShowStep(const QString& text);
    void Tick();
    bool HandleKey(QKeyEvent* event, bool isDown);
    void RunInputTest();
    void RunAudioTest();
    void RunConsoleTest();
    void RunKeysTest();
    void PressConsole(ConsoleKey key);
    void TapPhone(int x, int y);
    PhoneScreen CurrentPhoneScreen() const;
    void ApplyConsoleEffect(const ConsoleEffect& effect);
    void FinishTest(int exitCode, const std::string& summary);
    void SaveTestShot(const char* name, bool isWholeWindow = false);
    IUsbBackend& m_backend;
    Logger& m_logger;
    TestMode m_mode;
    std::shared_ptr<ProjectionInput> m_input;
    ConsoleController m_console;
    std::shared_ptr<AudioState> m_audioState;
    std::unique_ptr<IAudioEngine> m_audio;
    DisplayConfig m_display{kDefaultDisplay};
    VideoWidget* m_video{};
    CarPanel* m_panel{};
    QLabel* m_status{};
    QLabel* m_step{};
    QComboBox* m_displayChoice{};
    QPushButton* m_button{};
    QPushButton* m_wirelessButton{};   // only where wireless Android Auto is built
    QPlainTextEdit* m_history{};
    std::atomic_bool m_isStopRequested{};
    std::mutex m_frameMutex;
    std::optional<VideoFrame> m_latestFrame;
    unsigned m_displayedFrames{};
    bool m_isCloseRequested{};
    State m_state{State::Idle};
    QFutureWatcher<AutoConnectResult> m_watcher;
    QFutureWatcher<UsbScanResult> m_scanWatcher;
    // Scripted test state.
    QElapsedTimer m_testClock;
    int m_testStage{};
    unsigned m_testFrames[4]{};
    int m_testExitCode{3};
    bool m_isTestFinished{};
    std::string m_testProblems;
    std::vector<std::string> m_testSteps;   // --test-keys script
};
}
