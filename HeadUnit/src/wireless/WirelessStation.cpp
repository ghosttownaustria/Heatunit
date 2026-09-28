#include "wireless/WirelessStation.h"
#include "wireless/SocketTransport.h"
#include "wireless/TcpListener.h"
#include "wireless/WirelessLink.h"
#include <memory>
#include <thread>
#include <utility>

namespace headunit {
namespace {
using namespace std::chrono_literals;
// After a failed start (Bluetooth not up yet at boot, no NetworkManager, ...) the next try comes this much later.
constexpr auto kRetryDelay = 60s;
// How long a phone that opened the Android Auto service waits for a Wi-Fi that is still starting (nmcli takes up to 45 s).
constexpr auto kWifiWait = 60s;
// How long the phone may take from the Bluetooth handshake to its TCP connection.
constexpr auto kLinkTimeout = 60s;
}

// A station that is not started yet. `events.onStatus` receives the steps for the window, the pairing events the
// question to show (BluetoothEvents).
WirelessStation::WirelessStation(Logger& logger, BluetoothEvents events)
    : m_logger(logger), m_onStatus(events.onStatus), m_settings(LoadWirelessSettings()), m_hotspot(logger), m_bluetooth(logger, std::move(events))
{
}

// Takes Bluetooth, the TCP port and the hotspot down.
WirelessStation::~WirelessStation()
{
    StopAll();
}

// Starts Bluetooth, then the hotspot in the background, and returns. Once the hotspot is up, the phones paired before
// are asked to connect (see BluetoothService::ConnectPairedPhones).
void WirelessStation::Start()
{
    StopAll();
    StartBluetooth();
    StartWifi();
}

// Waits up to `timeout` for a phone that opened the Android Auto service: its RFCOMM socket, or -1. Starts or restarts
// everything first when that is due.
int WirelessStation::WaitForPhone(std::chrono::milliseconds timeout)
{
    KeepRunning();
    if (!m_bluetooth.IsRunning()) {
        std::this_thread::sleep_for(timeout);
        return -1;
    }
    return m_bluetooth.WaitForPhone(timeout);
}

// The person asked for Android Auto: the paired phones are asked to connect, a connected one after reconnecting it (that
// starts Android Auto on the phone again). False when Bluetooth or the Wi-Fi is not up.
bool WirelessStation::ReconnectPhones()
{
    if (!m_bluetooth.IsRunning() || !m_isWifiReady) return false;
    m_bluetooth.ConnectPairedPhones(true);
    return true;
}

// For that socket (taken over and closed here): the Wi-Fi details over Bluetooth (once a hotspot that is still starting
// is up), then the Android Auto session over the TCP connection the phone opens.
AutoConnectResult WirelessStation::Serve(int rfcommFd, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
{
    // The Bluetooth link stays open until the session is over: the phone treats it as the car being connected.
    const FileDescriptor phone(rfcommFd);
    AutoConnectResult result;
    WaitForWifi(isStopRequested);
    if (!m_isWifiReady) {
        result.isStoppedByUser = isStopRequested;
        result.message = "Das WLAN fuer kabelloses Android Auto ist nicht bereit" + (m_wifiError.empty() ? std::string(".") : ": " + m_wifiError);
        return result;
    }
    Report("Handy verbindet sich kabellos; es bekommt die WLAN-Daten ueber Bluetooth ...");
    const auto link = EstablishWirelessLink(phone.Get(), m_listener.Get(), m_credentials, m_logger, isStopRequested, kLinkTimeout);
    if (link.tcpFd < 0) {
        result.isStoppedByUser = isStopRequested;
        result.message = link.message;
        if (link.hasSentInfo && !isStopRequested && m_settings.hotspot.isHidden)
            result.message += " Das WLAN ist verborgen (HEADUNIT_WIFI_HIDDEN); Android Auto findet es dann meist nicht.";
        return result;
    }
    Report("Handy im WLAN verbunden (" + link.peer + "); starte Android Auto.");
    const auto session = RunAndroidAutoSession(std::make_shared<SocketTransport>(link.tcpFd), m_logger, isStopRequested, std::move(callbacks));
    result.hasVideo = session.hasVideo;
    result.isStoppedByUser = session.isStoppedByUser;
    result.message = session.message;
    return result;
}

// The person quit HeadUnit with its button, as when a car is switched off: everything is taken down, then the Bluetooth
// adapter (which drops every phone's Bluetooth link) and the Wi-Fi chip are switched off. The next start switches both
// on again.
void WirelessStation::SwitchRadiosOff()
{
    StopAll();
    m_bluetooth.SwitchOff();
    SwitchWifiOff(m_logger);
}

// A step for the log and the window.
void WirelessStation::Report(const std::string& text)
{
    m_logger.Write(LogLevel::Info, "WLAN", text);
    if (m_onStatus) m_onStatus(text);
}

// A failed start: shown on the window when the cause is new, otherwise only logged.
void WirelessStation::ReportFailure(std::string& lastError, const std::string& error, const std::string& text)
{
    if (error != lastError) Report(text);
    else m_logger.Write(LogLevel::Info, "WLAN", text);
    lastError = error;
}

// Waits for a hotspot start under way, then takes everything down.
void WirelessStation::StopAll()
{
    if (m_wifiStart.valid()) m_wifiStart.wait();
    m_wifiStart = {};
    m_isWifiReady = false;
    m_bluetooth.Stop();
    m_listener.Reset();
    m_hotspot.Stop();
}

// Bluetooth comes first and takes a second or two: the phone finds HEATUNIT as soon as the head unit is on.
void WirelessStation::StartBluetooth()
{
    const auto& name = m_settings.bluetoothName;
    std::string error = m_bluetooth.Start(name);
    if (error.empty()) error = OpenListener();
    if (!error.empty()) {
        m_bluetooth.Stop();
        m_nextBluetoothStart = Clock::now() + kRetryDelay;
        ReportFailure(m_bluetoothError, error, "Bluetooth fuer kabelloses Android Auto ist nicht bereit: " + error + " Neuer Versuch in einer Minute; USB geht weiter.");
        return;
    }
    m_bluetoothError.clear();
    Report("Bluetooth sichtbar als '" + name + "'. Neues Handy: in seinen Bluetooth-Einstellungen " + name + " waehlen und koppeln.");
    if (m_isWifiReady) m_bluetooth.ConnectPairedPhones();
}

// Opens the TCP port the phone connects to, unless it is open already; empty on success.
std::string WirelessStation::OpenListener()
{
    if (m_listener.IsValid()) return {};
    std::string error;
    m_listener.Reset(ListenTcp(kWirelessPort, error));
    if (!m_listener.IsValid() && error.empty()) error = "Port " + std::to_string(kWirelessPort) + " laesst sich nicht oeffnen.";
    return error;
}

// Starts the hotspot in the background; CheckWifi takes the result.
void WirelessStation::StartWifi()
{
    m_isWifiReady = false;
    const auto config = m_settings.hotspot;
    m_logger.Write(LogLevel::Info, "WLAN", "Starting the Wi-Fi '" + config.ssid + "'" + (config.isHidden ? " (hidden)" : "") + " in the background");
    m_wifiStart = std::async(std::launch::async, [this, config] { return StartHotspot(config); });
}

// Starts the hotspot and keeps its details in m_network (background thread). nmcli needs a few seconds to bring the
// access point up; the log says how long.
std::string WirelessStation::StartHotspot(const HotspotConfig& config)
{
    const auto started = Clock::now();
    HotspotInfo info;
    auto error = m_hotspot.Start(config, info);
    if (!error.empty()) return error;
    m_network = info;
    m_logger.Write(LogLevel::Info, "WLAN", "Hotspot ready after " + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count()) + " ms");
    return {};
}

// Takes the result of the background start once it is there, waiting up to `wait` for it.
void WirelessStation::CheckWifi(std::chrono::milliseconds wait)
{
    if (!m_wifiStart.valid() || m_wifiStart.wait_for(wait) != std::future_status::ready) return;
    const auto error = m_wifiStart.get();
    if (!error.empty()) {
        m_nextWifiStart = Clock::now() + kRetryDelay;
        ReportFailure(m_wifiError, error, "Das WLAN fuer kabelloses Android Auto ist nicht bereit: " + error + " Neuer Versuch in einer Minute; USB geht weiter.");
        return;
    }
    m_wifiError.clear();
    m_credentials = {m_network.ssid, m_network.password, m_network.bssid, m_network.ipAddress, kWirelessPort};
    m_isWifiReady = true;
    Report("Kabellos bereit: per Bluetooth sichtbar als '" + m_settings.bluetoothName + "', WLAN '" + m_network.ssid + "'" +
        (m_settings.hotspot.isHidden ? " (verborgen)" : "") + " laeuft.");
    // The phones paired before are asked only now, when the Wi-Fi they are sent to is there.
    m_bluetooth.ConnectPairedPhones();
}

// Starts what is not running, when its time has come.
void WirelessStation::KeepRunning()
{
    const auto now = Clock::now();
    if (!m_bluetooth.IsRunning() && now >= m_nextBluetoothStart) StartBluetooth();
    CheckWifi();
    if (!m_isWifiReady && !m_wifiStart.valid() && now >= m_nextWifiStart) StartWifi();
}

// The phone came before the Wi-Fi was up (it was quick, or the last start failed): it waits for the start, which is
// requested now when none is under way.
void WirelessStation::WaitForWifi(const std::atomic_bool& isStopRequested)
{
    if (m_isWifiReady) return;
    Report("Handy verbindet sich kabellos; das WLAN startet noch ...");
    if (!m_wifiStart.valid()) StartWifi();
    const auto deadline = Clock::now() + kWifiWait;
    while (!isStopRequested && m_wifiStart.valid() && Clock::now() < deadline) CheckWifi(200ms);
}
}
