#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/AutoConnect.h"
#include "logging/Logger.h"
#include "wireless/Hotspot.h"
#include <atomic>
#include <chrono>
#include <string>

namespace headunit {
// What the user can change, from environment variables (the password is remembered between runs):
//   HEADUNIT_BT_NAME       Bluetooth name the phone sees (default HEATUNIT)
//   HEADUNIT_WIFI_SSID     name of the Wi-Fi network the head unit creates (default HEATUNIT-AA)
//   HEADUNIT_WIFI_PASSWORD its password (default: 16 random characters, kept in the user's settings)
//   HEADUNIT_WIFI_INTERFACE the Wi-Fi device (default: the first one NetworkManager knows)
//   HEADUNIT_WIFI_BAND     a = 5 GHz (default), bg = 2.4 GHz
//   HEADUNIT_WIFI_CHANNEL  default 36 (5 GHz) or 6 (2.4 GHz)
struct WirelessSettings {
    std::string bluetoothName{"HEATUNIT"};
    HotspotConfig hotspot;
};
WirelessSettings LoadWirelessSettings();

// "Android Auto kabellos": switches Bluetooth on and becomes visible, waits for the paired phone to open the Android
// Auto service, only then starts the hotspot, hands the phone the Wi-Fi details, and runs the Android Auto session over
// the TCP connection the phone opens. The hotspot goes down again after every attempt, so it is only on the air while
// a phone connects. Returns when a session that showed video has ended, the user stopped it, or something failed for
// good. Runs on a worker thread that can run Qt events (the D-Bus calls of Bluetooth arrive there).
AutoConnectResult ConnectWirelessAndroidAuto(Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);

// Only the Bluetooth part, for finding out whether a phone pairs and connects: visible under the Bluetooth name,
// waits up to `duration`, and logs what the phone does and says. Returns 0 when a phone opened the service and
// talked through the handshake as far as the Wi-Fi details.
int RunBluetoothTest(Logger& logger, std::chrono::seconds duration);
// Only the hotspot: starts it, logs and prints how a phone would join, keeps it for `duration`, stops it.
int RunHotspotTest(Logger& logger, std::chrono::seconds duration);
}
