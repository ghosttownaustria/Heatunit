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
namespace {
using namespace std::chrono_literals;
constexpr const char* kConnectionName = "HeadUnit-AP";

// `text` without white space at either end.
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

// The first Wi-Fi device of `nmcli -t -f DEVICE,TYPE device` ("wlan0:wifi" lines).
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

// The IPv4 address of the interface, empty when it has none (yet).
std::string Ipv4Of(const std::string& interfaceName)
{
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return {};
    std::string address;
    for (const ifaddrs* entry = list; entry; entry = entry->ifa_next) {
        if (!entry->ifa_addr || entry->ifa_addr->sa_family != AF_INET || interfaceName != entry->ifa_name) continue;
        char text[INET_ADDRSTRLEN]{};
        const auto* socketAddress = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
        if (inet_ntop(AF_INET, &socketAddress->sin_addr, text, sizeof(text))) {
            address = text;
            break;
        }
    }
    freeifaddrs(list);
    return address;
}

// The MAC address of the interface, in lower case as Android writes BSSIDs in its scan results (and as sysfs has it).
std::string MacOf(const std::string& interfaceName)
{
    std::ifstream file("/sys/class/net/" + interfaceName + "/address");
    std::string mac;
    std::getline(file, mac);
    mac = Trimmed(mac);
    std::transform(mac.begin(), mac.end(), mac.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return mac;
}

// Whether NetworkManager has the hotspot's connection profile.
bool HasConnectionProfile()
{
    const auto listing = RunCommand({"nmcli", "-t", "-f", "NAME", "connection", "show"}, 10s);
    std::istringstream lines(listing.output);
    for (std::string line; std::getline(lines, line);) {
        if (Trimmed(line) == kConnectionName) return true;
    }
    return false;
}

// Removes the hotspot's connection profile (a leftover would keep old settings).
void DeleteConnectionProfile(std::chrono::seconds timeout)
{
    RunCommand({"nmcli", "connection", "delete", "id", kConnectionName}, timeout);
}

// The address NetworkManager assigns a moment after the connection is up; empty when none came within ten seconds.
std::string WaitForAddress(const std::string& interfaceName)
{
    std::string address;
    for (int attempt = 0; attempt < 50 && address.empty(); ++attempt) {
        address = Ipv4Of(interfaceName);
        if (address.empty()) std::this_thread::sleep_for(200ms);
    }
    return address;
}
}

// A hotspot that is not running yet.
Hotspot::Hotspot(Logger& logger) : m_logger(logger)
{
}

// Takes the hotspot down.
Hotspot::~Hotspot()
{
    Stop();
}

// Starts the hotspot and fills `info`. Empty on success, otherwise what went wrong and what to do about it (German, for
// the window).
std::string Hotspot::Start(const HotspotConfig& config, HotspotInfo& info)
{
    Stop();
    if (config.password.size() < 8 || config.password.size() > 63) return "Das WLAN-Passwort muss 8 bis 63 Zeichen lang sein.";
    const auto version = RunCommand({"nmcli", "--version"}, 5s);
    if (!version.isStarted || version.exitCode != 0)
        return "NetworkManager (nmcli) ist nicht installiert. Raspberry Pi OS Bookworm bringt es mit; sonst: sudo apt install network-manager";

    std::string interfaceName = config.interfaceName;
    if (interfaceName.empty()) {
        interfaceName = FirstWifiDevice(RunCommand({"nmcli", "-t", "-f", "DEVICE,TYPE", "device"}, 10s).output);
        if (interfaceName.empty()) return "NetworkManager kennt keinen WLAN-Chip. Pruefen mit: nmcli device   (WLAN mit rfkill unblock wifi einschalten)";
    }
    m_logger.Write(LogLevel::Info, "WLAN", "Hotspot on interface " + interfaceName + ", network '" + config.ssid + "'" + (config.isHidden ? " (hidden)" : "") +
        ", band " + config.band + ", channel " + std::to_string(config.channel));

    RunCommand({"nmcli", "radio", "wifi", "on"}, 10s);
    DeleteConnectionProfile(15s);
    // WPA2-PSK with CCMP, what the phone is told (kWifiSecurityWpa2Personal). Protected management frames (PMF, 802.11w)
    // are switched off: NetworkManager offers them by default, the Raspberry Pi's Wi-Fi chip (brcmfmac) does not handle
    // them as an access point, and a phone that uses them (current Android phones do) then fails the WPA2 handshake and
    // reports "wrong password" although the password is right.
    const auto added = RunCommand({"nmcli", "connection", "add", "type", "wifi", "ifname", interfaceName, "con-name", kConnectionName,
        "autoconnect", "no", "ssid", config.ssid, "mode", "ap", "802-11-wireless.hidden", config.isHidden ? "yes" : "no",
        "802-11-wireless.band", config.band, "802-11-wireless.channel", std::to_string(config.channel),
        "ipv4.method", "shared", "ipv6.method", "ignore",
        "wifi-sec.key-mgmt", "wpa-psk", "wifi-sec.proto", "rsn", "wifi-sec.pairwise", "ccmp", "wifi-sec.group", "ccmp",
        "wifi-sec.pmf", "disable", "wifi-sec.psk", config.password}, 20s);
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
    const std::string address = WaitForAddress(interfaceName);
    if (address.empty()) {
        Stop();
        return "Der WLAN-Hotspot laeuft, hat aber keine IP-Adresse bekommen (Schnittstelle " + interfaceName + ").";
    }
    info.interfaceName = interfaceName;
    info.ssid = config.ssid;
    info.password = config.password;
    info.ipAddress = address;
    info.bssid = MacOf(interfaceName);
    m_logger.Write(LogLevel::Info, "WLAN", "Hotspot up: " + info.ipAddress + ", BSSID " + info.bssid);
    return {};
}

// Takes the hotspot down and removes its profile; the previous Wi-Fi connection returns.
void Hotspot::Stop()
{
    if (!m_isStarted) return;
    m_isStarted = false;
    RunCommand({"nmcli", "connection", "down", "id", kConnectionName}, 20s);
    DeleteConnectionProfile(15s);
    m_logger.Write(LogLevel::Info, "WLAN", "Hotspot stopped");
}

// A run that was killed (Ctrl+C, crash) cannot take its hotspot down, and NetworkManager keeps it up until the next
// reboot. Removes it; nothing happens when there is none or NetworkManager is missing.
void RemoveLeftoverHotspot(Logger& logger)
{
    if (!HasConnectionProfile()) return;
    DeleteConnectionProfile(20s);
    logger.Write(LogLevel::Info, "WLAN", "Removed the hotspot an earlier run left behind");
}

// Switches the Wi-Fi chip off (nmcli radio wifi off), also for other networks; Hotspot::Start switches it on again.
void SwitchWifiOff(Logger& logger)
{
    const auto result = RunCommand({"nmcli", "radio", "wifi", "off"}, 10s);
    if (result.isStarted && result.exitCode == 0) logger.Write(LogLevel::Info, "WLAN", "Wi-Fi switched off");
    else logger.Write(LogLevel::Warning, "WLAN", "Switching the Wi-Fi off failed: " + Brief(result));
}
}
