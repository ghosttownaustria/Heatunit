#pragma once
#include "logging/Logger.h"
#include <string>

namespace headunit {
struct HotspotConfig {
    std::string interfaceName;   // empty: the first Wi-Fi device NetworkManager knows
    std::string ssid{"HEATUNIT-AA"};
    std::string password;        // WPA2, 8 to 63 characters
    std::string band{"a"};       // "a" = 5 GHz, "bg" = 2.4 GHz
    int channel{36};
};
struct HotspotInfo {
    std::string interfaceName;
    std::string ssid;
    std::string password;
    std::string bssid;           // MAC address of the access point, "AA:BB:CC:DD:EE:FF"
    std::string ipAddress;       // the head unit's address in the hotspot network
};

// The Wi-Fi network the phone joins for wireless Android Auto, made by NetworkManager (nmcli): the head unit's
// Wi-Fi chip becomes an access point, and NetworkManager hands out addresses (ipv4.method shared).
// While it runs, the same chip cannot be a Wi-Fi client, so an SSH session over Wi-Fi ends. The previous
// Wi-Fi connection returns when the hotspot is stopped.
class Hotspot {
public:
    explicit Hotspot(Logger& logger) : m_logger(logger) {}
    ~Hotspot() { Stop(); }
    Hotspot(const Hotspot&) = delete;
    Hotspot& operator=(const Hotspot&) = delete;
    // Empty on success, otherwise what went wrong and what to do about it (German, for the window).
    std::string Start(const HotspotConfig& config, HotspotInfo& info);
    void Stop();
private:
    Logger& m_logger;
    bool m_isStarted{};
};
}
