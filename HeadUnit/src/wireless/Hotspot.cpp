#include "wireless/Hotspot.h"
#include "wireless/SystemCommand.h"
#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <chrono>
#include <fstream>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>

namespace headunit {
using namespace std::chrono_literals;
namespace {
constexpr const char* kConnectionName = "HeadUnit-AP";

std::string Trimmed(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    std::size_t first = 0;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first]))) ++first;
    return text.substr(first);
}

// nmcli's message, shortened so that it fits in the window's status line.
std::string Brief(const CommandResult& result)
{
    auto text = Trimmed(result.output);
    if (result.isTimedOut) return "Zeitueberschreitung";
    if (text.size() > 300) text.resize(300);
    return text.empty() ? "Exit-Code " + std::to_string(result.exitCode) : text;
}

// "wlan0:wifi" lines of `nmcli -t -f DEVICE,TYPE device`.
std::string FirstWifiDevice(const std::string& listing)
{
    std::istringstream lines(listing);
    for (std::string line; std::getline(lines, line);) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        if (line.substr(colon + 1) == "wifi") return line.substr(0, colon);
    }
    return {};
}

std::string Ipv4Of(const std::string& interfaceName)
{
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return {};
    std::string address;
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || interfaceName != entry->ifa_name) continue;
        char text[INET_ADDRSTRLEN]{};
        const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &socketAddress->sin_addr, text, sizeof(text))) { address = text; break; }
    }
    freeifaddrs(list);
    return address;
}

std::string MacOf(const std::string& interfaceName)
{
    std::ifstream file("/sys/class/net/" + interfaceName + "/address");
    std::string mac;
    std::getline(file, mac);
    // Lower case, as Android writes BSSIDs in its scan results (and as sysfs has it already).
    mac = Trimmed(mac);
    std::transform(mac.begin(), mac.end(), mac.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return mac;
}

bool HasConnectionProfile()
{
    const auto listing = RunCommand({"nmcli", "-t", "-f", "NAME", "connection", "show"}, 10s);
    std::istringstream lines(listing.output);
    for (std::string line; std::getline(lines, line);)
        if (Trimmed(line) == kConnectionName) return true;
    return false;
}
}

void RemoveLeftoverHotspot(Logger& logger)
{
    if (!HasConnectionProfile()) return;
    RunCommand({"nmcli", "connection", "delete", "id", kConnectionName}, 20s);
    logger.Write("INFO", "WLAN", "Removed the hotspot an earlier run left behind");
}

std::string Hotspot::Start(const HotspotConfig& config, HotspotInfo& info)
{
    Stop();
    if (config.password.size() < 8 || config.password.size() > 63) return "Das WLAN-Passwort muss 8 bis 63 Zeichen lang sein.";

    const auto version = RunCommand({"nmcli", "--version"}, 5s);
    if (!version.isStarted || version.exitCode != 0)
        return "NetworkManager (nmcli) ist nicht installiert. Raspberry Pi OS Bookworm bringt es mit; sonst: sudo apt install network-manager";

    std::string interfaceName = config.interfaceName;
    if (interfaceName.empty()) {
        const auto devices = RunCommand({"nmcli", "-t", "-f", "DEVICE,TYPE", "device"}, 10s);
        interfaceName = FirstWifiDevice(devices.output);
        if (interfaceName.empty()) return "NetworkManager kennt keinen WLAN-Chip. Pruefen mit: nmcli device   (WLAN mit rfkill unblock wifi einschalten)";
    }
    m_logger.Write("INFO", "WLAN", "Hotspot on interface " + interfaceName + ", network '" + config.ssid + "', band " + config.band + ", channel " + std::to_string(config.channel));

    RunCommand({"nmcli", "radio", "wifi", "on"}, 10s);
    // A leftover from an earlier run (or a crash) would keep the old settings.
    RunCommand({"nmcli", "connection", "delete", "id", kConnectionName}, 15s);

    const auto added = RunCommand({"nmcli", "connection", "add", "type", "wifi", "ifname", interfaceName, "con-name", kConnectionName,
        "autoconnect", "no", "ssid", config.ssid, "mode", "ap",
        "802-11-wireless.band", config.band, "802-11-wireless.channel", std::to_string(config.channel),
        "ipv4.method", "shared", "ipv6.method", "ignore",
        "wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.proto", "rsn", "wifi-sec.pairwise", "ccmp", "wifi-sec.group", "ccmp",
        "wifi-sec.psk", config.password}, 20s);
    if (added.exitCode != 0) return "Der WLAN-Hotspot konnte nicht angelegt werden: " + Brief(added);
    m_isStarted = true;

    const auto up = RunCommand({"nmcli", "connection", "up", "id", kConnectionName}, 45s);
    if (up.exitCode != 0) {
        const auto message = "Der WLAN-Hotspot startet nicht: " + Brief(up) +
            ". Haeufige Ursachen: WLAN-Laendercode nicht gesetzt (sudo raspi-config, Localisation Options, WLAN Country), "
            "oder der WLAN-Chip kann den Kanal nicht als Access Point (Band 'bg' probieren: HEADUNIT_WIFI_BAND=bg HEADUNIT_WIFI_CHANNEL=6).";
        Stop();
        return message;
    }

    // NetworkManager assigns the address a moment after the connection is up.
    std::string address;
    for (int attempt = 0; attempt < 50 && address.empty(); ++attempt) {
        address = Ipv4Of(interfaceName);
        if (address.empty()) std::this_thread::sleep_for(200ms);
    }
    if (address.empty()) { Stop(); return "Der WLAN-Hotspot laeuft, hat aber keine IP-Adresse bekommen (Schnittstelle " + interfaceName + ")."; }

    info.interfaceName = interfaceName;
    info.ssid = config.ssid;
    info.password = config.password;
    info.ipAddress = address;
    info.bssid = MacOf(interfaceName);
    m_logger.Write("INFO", "WLAN", "Hotspot up: " + info.ipAddress + ", BSSID " + info.bssid);
    return {};
}

void Hotspot::Stop()
{
    if (!m_isStarted) return;
    m_isStarted = false;
    RunCommand({"nmcli", "connection", "down", "id", kConnectionName}, 20s);
    RunCommand({"nmcli", "connection", "delete", "id", kConnectionName}, 15s);
    m_logger.Write("INFO", "WLAN", "Hotspot stopped");
}
}
