#pragma once
#include "usb/AutoConnectSystem.h"
#include "usb/IUsbBackend.h"
#include "logging/Logger.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <atomic>
#include <mutex>
#include <optional>
class QCloseEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;

namespace headunit {
// One button: it connects (finding the phone, repairing the driver, starting Android Auto,
// restarting the USB link when needed) and, while running, ends the session again.
class MainWindow final : public QMainWindow {
public:
    MainWindow(IUsbBackend& backend, Logger& logger, bool isSmokeTest, bool isProjectionTest = false);
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    enum class State { Idle, Connecting, Stopping };
    void SetState(State state);
    void OnButton();
    void StartConnect();
    void RequestStop();
    void FinishConnect(const AutoConnectResult& result);
    void ShowStep(const QString& text);
    IUsbBackend& m_backend;
    Logger& m_logger;
    bool m_isSmokeTest;
    bool m_isProjectionTest;
    QLabel* m_video{};
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
};
}
