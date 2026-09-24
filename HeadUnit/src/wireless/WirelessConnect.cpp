#include "wireless/WirelessConnect.h"
#include "platform/Environment.h"
#include "wireless/BluetoothService.h"
#include "wireless/SocketTransport.h"
#include "wireless/WirelessLink.h"
#include "wireless/WirelessProtocol.h"
#include <QSettings>
#include <QString>
#include <charconv>
#include <iostream>
#include <memory>
#include <random>
#include <thread>
#include <unistd.h>

namespace headunit {
using namespace std::chrono_literals;
namespace {
constexpr const char* kSettingsOrganization = "HeadUnit";
constexpr const char* kSettingsApplication = "HeadUnit";
constexpr const char* kPasswordSetting = "wireless/wifiPassword";
// After a failed start (Bluetooth not up yet at boot, no NetworkManager, ...) the next try comes this much later.
constexpr auto kRetryDelay = 60s;

// Closes a socket when it goes out of scope.
struct Socket {
    explicit Socket(int descriptor = -1) : fd(descriptor) {}
    ~Socket() { Close(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    void Close() { if (fd >= 0) { ::close(fd); fd = -1; } }
    int fd;
};

std::string RandomPassword()
{
    static constexpr char kAlphabet[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";   // no look-alike characters
    std::random_device device;
    std::uniform_int_distribution<std::size_t> pick(0, sizeof(kAlphabet) - 2);
    std::string password;
    for (int index = 0; index < 16; ++index) password += kAlphabet[pick(device)];
    return password;
}

// The phone joins this network by itself, told over Bluetooth, so the password never has to be typed: it only has to
// stay the same between runs, so that the phone's remembered copy of the network keeps working.
std::string RememberedPassword()
{
    QSettings settings(kSettingsOrganization, kSettingsApplication);
    auto password = settings.value(kPasswordSetting).toString().toStdString();
    if (password.size() < 8) {
        password = RandomPassword();
        settings.setValue(kPasswordSetting, QString::fromStdString(password));
        settings.sync();
    }
    return password;
}

// nmcli needs a few seconds to bring the access point up; the log says how long.
std::string StartHotspot(Hotspot& hotspot, const HotspotConfig& config, HotspotInfo& network, Logger& logger)
{
    const auto started = std::chrono::steady_clock::now();
    auto error = hotspot.Start(config, network);
    if (error.empty())
        logger.Write("INFO", "WLAN", "Hotspot ready after " +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms");
    return error;
}
}

WirelessSettings LoadWirelessSettings()
{
    WirelessSettings settings;
    if (const auto name = GetEnv("HEADUNIT_BT_NAME"); name && !name->empty()) settings.bluetoothName = *name;
    auto& hotspot = settings.hotspot;
    if (const auto ssid = GetEnv("HEADUNIT_WIFI_SSID"); ssid && !ssid->empty()) hotspot.ssid = *ssid;
    if (const auto interfaceName = GetEnv("HEADUNIT_WIFI_INTERFACE")) hotspot.interfaceName = *interfaceName;
    const auto password = GetEnv("HEADUNIT_WIFI_PASSWORD");
    hotspot.password = password && !password->empty() ? *password : RememberedPassword();
    if (const auto band = GetEnv("HEADUNIT_WIFI_BAND"); band && (*band == "a" || *band == "bg")) {
        hotspot.band = *band;
        hotspot.channel = *band == "a" ? 36 : 6;
    }
    if (const auto channel = GetEnv("HEADUNIT_WIFI_CHANNEL")) {
        int value = 0;
        if (std::from_chars(channel->data(), channel->data() + channel->size(), value).ec == std::errc{} && value > 0) hotspot.channel = value;
    }
    if (const auto hidden = GetEnv("HEADUNIT_WIFI_HIDDEN"); hidden && (*hidden == "0" || *hidden == "no" || *hidden == "false")) hotspot.isHidden = false;
    return settings;
}

struct WirelessStation::Impl {
    Impl(Logger& log, std::function<void(const std::string&)> status)
        : logger(log), onStatus(std::move(status)), hotspot(log), bluetooth(log) {}
    Logger& logger;
    std::function<void(const std::string&)> onStatus;
    WirelessSettings settings{LoadWirelessSettings()};
    Hotspot hotspot;
    BluetoothService bluetooth;
    Socket listener;
    WifiCredentials credentials;
    bool isReady{};
    std::string lastError;
    std::chrono::steady_clock::time_point nextStart{};

    void Report(const std::string& text)
    {
        logger.Write("INFO", "WLAN", text);
        if (onStatus) onStatus(text);
    }
    void Stop()
    {
        isReady = false;
        bluetooth.Stop();
        listener.Close();
        hotspot.Stop();
    }
    // The Wi-Fi comes first: the phone expects the start request as soon as it has opened the Android Auto service,
    // and must not wait for nmcli then.
    void Start()
    {
        Stop();
        const auto& wifi = settings.hotspot;
        Report("Starte das WLAN '" + wifi.ssid + "'" + (wifi.isHidden ? " (verborgen)" : "") + " und Bluetooth ...");
        HotspotInfo network;
        std::string error = StartHotspot(hotspot, wifi, network, logger);
        if (error.empty()) error = bluetooth.Start(settings.bluetoothName);
        if (error.empty()) listener.fd = ListenTcp(kWirelessPort, error);
        if (listener.fd < 0 && error.empty()) error = "Port " + std::to_string(kWirelessPort) + " laesst sich nicht oeffnen.";
        if (!error.empty()) {
            Stop();
            nextStart = std::chrono::steady_clock::now() + kRetryDelay;
            const std::string text = "Kabelloses Android Auto ist nicht bereit: " + error + " Neuer Versuch in einer Minute; USB geht weiter.";
            // A cause that stays is reported once; the retries only go to the log.
            if (error != lastError) Report(text);
            else logger.Write("INFO", "WLAN", text);
            lastError = error;
            return;
        }
        lastError.clear();
        credentials = {network.ssid, network.password, network.bssid, network.ipAddress, kWirelessPort};
        isReady = true;
        Report("Kabellos bereit: WLAN '" + network.ssid + "'" + (wifi.isHidden ? " (verborgen)" : "") + " laeuft, per Bluetooth sichtbar als '" +
            settings.bluetoothName + "'. Neues Handy: einmal in seinen Bluetooth-Einstellungen mit " + settings.bluetoothName + " koppeln.");
        bluetooth.ConnectPairedPhones();
    }
};

WirelessStation::WirelessStation(Logger& logger, std::function<void(const std::string&)> onStatus)
    : m_impl(std::make_unique<Impl>(logger, std::move(onStatus))) {}
WirelessStation::~WirelessStation() { m_impl->Stop(); }

void WirelessStation::Start() { m_impl->Start(); }

int WirelessStation::WaitForPhone(std::chrono::milliseconds timeout)
{
    auto& impl = *m_impl;
    if (!impl.isReady && std::chrono::steady_clock::now() >= impl.nextStart) impl.Start();
    if (!impl.isReady) {
        std::this_thread::sleep_for(timeout);
        return -1;
    }
    return impl.bluetooth.WaitForPhone(timeout);
}

AutoConnectResult WirelessStation::Serve(int rfcommFd, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
{
    auto& impl = *m_impl;
    Socket phone(rfcommFd);
    AutoConnectResult result;
    if (!impl.isReady) { result.message = "Kabelloses Android Auto ist nicht bereit."; return result; }
    impl.Report("Handy verbindet sich kabellos; es bekommt die WLAN-Daten ueber Bluetooth ...");
    auto link = EstablishWirelessLink(phone.fd, impl.listener.fd, impl.credentials, impl.logger, isStopRequested, 60s);
    if (link.tcpFd < 0) {
        result.isStoppedByUser = isStopRequested;
        result.message = link.message;
        if (link.hasSentInfo && impl.settings.hotspot.isHidden)
            result.message += " Das WLAN ist verborgen; findet das Handy es nicht, mit HEADUNIT_WIFI_HIDDEN=0 sichtbar machen.";
        return result;
    }
    impl.Report("Handy im WLAN verbunden (" + link.peer + "); starte Android Auto.");
    // The Bluetooth link stays open until the session is over: the phone treats it as the car being connected.
    const auto session = RunAndroidAutoSession(std::make_shared<SocketTransport>(link.tcpFd), impl.logger, isStopRequested, std::move(callbacks));
    result.hasVideo = session.hasVideo;
    result.isStoppedByUser = session.isStoppedByUser;
    result.message = session.message;
    return result;
}

int RunBluetoothTest(Logger& logger, std::chrono::seconds duration)
{
    const auto settings = LoadWirelessSettings();
    BluetoothService bluetooth(logger);
    if (const auto error = bluetooth.Start(settings.bluetoothName); !error.empty()) {
        logger.Write("ERROR", "BT", error);
        std::cerr << error << '\n';
        return 3;
    }
    std::cout << "Bluetooth sichtbar als '" << settings.bluetoothName << "' fuer " << duration.count()
              << " Sekunden. Am Handy in den Bluetooth-Einstellungen koppeln und Android Auto starten; Details in headunit.log.\n"
              << "(Ohne WLAN: das Handy bekommt Platzhalter-Zugangsdaten und kann nicht beitreten. Der Test prueft nur Bluetooth.)\n" << std::flush;
    bluetooth.ConnectPairedPhones();
    const WifiCredentials placeholder{settings.hotspot.ssid, settings.hotspot.password, "00:00:00:00:00:00", "10.42.0.1", kWirelessPort};
    const std::atomic_bool neverStopped{false};
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        Socket phone(bluetooth.WaitForPhone(500ms));
        if (phone.fd < 0) continue;
        const auto link = EstablishWirelessLink(phone.fd, -1, placeholder, logger, neverStopped, 60s);
        logger.Write(link.hasSentInfo ? "INFO" : "ERROR", "TEST", "Bluetooth test: " + link.message);
        if (link.hasSentInfo) { std::cout << "Bluetooth-Test bestanden: das Handy hat nach den WLAN-Daten gefragt und sie bekommen.\n"; return 0; }
    }
    logger.Write("ERROR", "TEST", "Bluetooth test: no phone opened the Android Auto Wireless service");
    std::cerr << "Kein Handy hat den Android-Auto-Dienst geoeffnet (siehe headunit.log: Kopplung? 'Connected'? RFCOMM channel?).\n";
    return 4;
}

int RunHotspotTest(Logger& logger, std::chrono::seconds duration)
{
    const auto settings = LoadWirelessSettings();
    RemoveLeftoverHotspot(logger);
    Hotspot hotspot(logger);
    HotspotInfo network;
    if (const auto error = hotspot.Start(settings.hotspot, network); !error.empty()) {
        logger.Write("ERROR", "TEST", error);
        std::cerr << error << '\n';
        return 3;
    }
    std::cout << "Hotspot laeuft: Netz '" << network.ssid << "'" << (settings.hotspot.isHidden ? " (verborgen: am Handy 'Netzwerk hinzufuegen' und den Namen eintippen)" : "")
              << ", Passwort '" << network.password << "', Adresse " << network.ipAddress
              << ", BSSID " << network.bssid << ". Zum Ausprobieren das Handy manuell beitreten lassen (" << duration.count() << " Sekunden).\n" << std::flush;
    std::this_thread::sleep_for(duration);
    return 0;
}
}
