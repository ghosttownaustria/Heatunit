#pragma once
#include "wireless/Hotspot.h"
#include <string>

namespace headunit {
// What the user can change, from environment variables (the password is remembered between runs):
//   HEADUNIT_BT_NAME        Bluetooth name the phone sees (default HEATUNIT)
//   HEADUNIT_WIFI_SSID      name of the Wi-Fi network the head unit creates (default HEATUNIT-AA)
//   HEADUNIT_WIFI_PASSWORD  its password (default: 16 random characters, kept in the user's settings)
//   HEADUNIT_WIFI_INTERFACE the Wi-Fi device (default: the first one NetworkManager knows)
//   HEADUNIT_WIFI_BAND      a = 5 GHz (default), bg = 2.4 GHz
//   HEADUNIT_WIFI_CHANNEL   default 36 (5 GHz) or 6 (2.4 GHz)
//   HEADUNIT_WIFI_HIDDEN    1 = the network name is never broadcast, 0 = always broadcast. Unset: hidden, until a phone
//                           that got the Wi-Fi details did not join the hidden network; from then on it is broadcast
//                           (remembered, see RememberVisibleWifi)
struct WirelessSettings {
    std::string bluetoothName{"HEATUNIT"};
    HotspotConfig hotspot;
    bool isVisibilityFixed{};   // HEADUNIT_WIFI_HIDDEN decides, the fallback to a visible network does not apply
};

WirelessSettings LoadWirelessSettings();
void RememberVisibleWifi();
}
