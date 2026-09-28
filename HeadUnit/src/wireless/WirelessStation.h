#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/AutoConnect.h"
#include "logging/Logger.h"
#include "wireless/BluetoothEvents.h"
#include "wireless/BluetoothService.h"
#include "wireless/FileDescriptor.h"
#include "wireless/Hotspot.h"
#include "wireless/WirelessProtocol.h"
#include "wireless/WirelessSettings.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>

namespace headunit {
// The wireless half of the automatic mode (RunPhoneWatch), always ready like a car: the hotspot, Bluetooth (visible and
// pairable as HEATUNIT, with the Android Auto Wireless service) and the TCP port stay up for as long as the object
// lives. When they cannot start (Bluetooth not up yet at boot, no NetworkManager, ...) they are tried again a minute
// later. Bluetooth comes up first (visible within a second or two), the hotspot starts in the background. Construct,
// use and destroy it on one worker thread; Bluetooth answers BlueZ on a thread of its own (see BluetoothService).
class WirelessStation {
public:
    WirelessStation(Logger& logger, BluetoothEvents events);
    ~WirelessStation();
    WirelessStation(const WirelessStation&) = delete;
    WirelessStation& operator=(const WirelessStation&) = delete;

    void Start();
    int WaitForPhone(std::chrono::milliseconds timeout);
    bool ReconnectPhones();
    AutoConnectResult Serve(int rfcommFd, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
    void SwitchRadiosOff();

private:
    using Clock = std::chrono::steady_clock;

    Logger& m_logger;
    std::function<void(const std::string&)> m_onStatus;
    WirelessSettings m_settings;
    Hotspot m_hotspot;
    BluetoothService m_bluetooth;
    FileDescriptor m_listener;
    // The Wi-Fi starts in the background (nmcli takes seconds), so that Bluetooth is visible at once and USB is watched
    // meanwhile. `m_network` is written by that start and read once it has finished.
    std::future<std::string> m_wifiStart;
    HotspotInfo m_network;
    WifiCredentials m_credentials;
    bool m_isWifiReady{};
    // The causes reported last: a cause that stays is shown once, the retries only go to the log.
    std::string m_bluetoothError;
    std::string m_wifiError;
    Clock::time_point m_nextBluetoothStart{};
    Clock::time_point m_nextWifiStart{};

    void Report(const std::string& text);
    void ReportFailure(std::string& lastError, const std::string& error, const std::string& text);
    void StopAll();
    void StartBluetooth();
    std::string OpenListener();
    void StartWifi();
    std::string StartHotspot(const HotspotConfig& config);
    void CheckWifi(std::chrono::milliseconds wait = std::chrono::milliseconds::zero());
    void KeepRunning();
    void WaitForWifi(const std::atomic_bool& isStopRequested);
    void ShowWifi();
};
}
