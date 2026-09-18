#pragma once
#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include "audio/WasapiAudioEngine.h"
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
// mouse input as touch.
class MainWindow final : public QMainWindow {
public:
    // The test modes run one scripted check against the real phone and exit with its result.
    enum class TestMode { None, Smoke, Projection, Input, Audio };
    MainWindow(IUsbBackend& backend, Logger& logger, TestMode mode = TestMode::None);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
private:
    enum class State { Idle, Connecting, Stopping };
    void SetState(State state);
    void OnButton();
    void StartConnect();
    void RequestStop();
    void FinishConnect(const AutoConnectResult& result);
    void ShowStep(const QString& text);
    void Tick();
    bool HandleKey(QKeyEvent* event, bool isDown);
    void RunInputTest();
    void RunAudioTest();
    void FinishTest(int exitCode, const std::string& summary);
    void SaveTestShot(const char* name, bool isWholeWindow = false);
    IUsbBackend& m_backend;
    Logger& m_logger;
    TestMode m_mode;
    std::shared_ptr<ProjectionInput> m_input;
    std::shared_ptr<AudioState> m_audioState;
    std::unique_ptr<WasapiAudioEngine> m_audio;
    VideoWidget* m_video{};
    CarPanel* m_panel{};
    QLabel* m_status{};
    QLabel* m_step{};
    QPushButton* m_button{};
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
};
}
