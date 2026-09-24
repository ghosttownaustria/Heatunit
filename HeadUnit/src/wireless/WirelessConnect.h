#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/AutoConnect.h"
#include "logging/Logger.h"
#include "wireless/Hotspot.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace headunit {
// What the user can change, from environment variables (the password is remembered between runs):
//   HEADUNIT_BT_NAME       Bluetooth name the phone sees (default HEATUNIT)
//   HEADUNIT_WIFI_SSID     name of the Wi-Fi network the head unit creates (default HEATUNIT-AA)
//   HEADUNIT_WIFI_PASSWORD its password (default: 16 random characters, kept in the user's settings)
//   HEADUNIT_WIFI_INTERFACE the Wi-Fi device (default: the first one NetworkManager knows)
//   HEADUNIT_WIFI_BAND     a = 5 GHz (default), bg = 2.4 GHz
//   HEADUNIT_WIFI_CHANNEL  default 36 (5 GHz) or 6 (2.4 GHz)
//   HEADUNIT_WIFI_HIDDEN   0 = the network name is broadcast (default: hidden, the phone learns it over Bluetooth)
struct WirelessSettings {
    std::string bluetoothName{"HEATUNIT"};
    HotspotConfig hotspot;
};
WirelessSettings LoadWirelessSettings();

// The wireless half of the automatic mode (RunPhoneWatch), always ready like a car: the hotspot, Bluetooth (visible and
// pairable as HEATUNIT, with the Android Auto Wireless service) and the TCP port stay up for as long as the object
// lives. When they cannot start (Bluetooth not up yet at boot, no NetworkManager, ...) they are tried again a minute
// later. Construct, use and destroy it on one worker thread that can run Qt events: the D-Bus calls of Bluetooth
// arrive there while WaitForPhone runs.
class WirelessStation {
public:
    // `onStatus` receives the steps for the window.
    WirelessStation(Logger& logger, std::function<void(const std::string&)> onStatus);
    ~WirelessStation();
    WirelessStation(const WirelessStation&) = delete;
    WirelessStation& operator=(const WirelessStation&) = delete;
    // Starts the hotspot, then Bluetooth, then asks the phones paired before to connect (see ConnectPairedPhones).
    void Start();
    // Waits up to `timeout` for a phone that opened the Android Auto service: its RFCOMM socket, or -1. Starts or
    // restarts everything first when that is due.
    int WaitForPhone(std::chrono::milliseconds timeout);
    // For that socket (taken over and closed here): the Wi-Fi details over Bluetooth at once, then the Android Auto
    // session over the TCP connection the phone opens.
    AutoConnectResult Serve(int rfcommFd, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Only the Bluetooth part, for finding out whether a phone pairs and connects: visible under the Bluetooth name,
// waits up to `duration`, and logs what the phone does and says. Returns 0 when a phone opened the service and
// talked through the handshake as far as the Wi-Fi details.
int RunBluetoothTest(Logger& logger, std::chrono::seconds duration);
// Only the hotspot: starts it, logs and prints how a phone would join, keeps it for `duration`, stops it.
int RunHotspotTest(Logger& logger, std::chrono::seconds duration);
}
