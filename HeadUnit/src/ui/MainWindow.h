#pragma once
#include "usb/IUsbBackend.h"
#include "usb/AndroidUsbProbe.h"
#include "logging/Logger.h"
#include <QFutureWatcher>
#include <QMainWindow>
#include <mutex>
#include <optional>
class QLabel;
class QPushButton;
class QTreeWidget;

namespace headunit {
class MainWindow final : public QMainWindow {
public:
    MainWindow(IUsbBackend& backend, Logger& logger, bool isSmokeTest, bool isProjectionTest = false);
    ~MainWindow() override;
private:
    void StartScan();
    void FinishScan();
    void StartUsbProbe(bool isStartAccessory = false, bool isProjection = false);
    IUsbBackend& m_backend;
    Logger& m_logger;
    bool m_isSmokeTest;
    QLabel* m_status{};
    QPushButton* m_scanButton{};
    QPushButton* m_probeButton{};
    QPushButton* m_accessoryButton{};
    QPushButton* m_connectButton{};
    QPushButton* m_stopButton{};
    QLabel* m_video{};
    std::atomic_bool m_isStopRequested{};
    std::mutex m_frameMutex;
    std::optional<VideoFrame> m_latestFrame;
    bool m_isProjectionTest{}, m_hasTestStarted{};
    unsigned m_displayedFrames{};
    bool m_isAccessoryAttempt{};
    QLabel* m_connectionStatus{};
    QTreeWidget* m_devices{};
    QFutureWatcher<UsbScanResult> m_watcher;
    QFutureWatcher<UsbProbeResult> m_probeWatcher;
    std::vector<UsbDevice> m_lastDevices;
};
}
