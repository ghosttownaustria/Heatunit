#include "wireless/WirelessConnect.h"
#include "platform/Environment.h"
#include "wireless/BluetoothService.h"
#include "wireless/SocketTransport.h"
#include "wireless/WirelessLink.h"
#include "wireless/WirelessProtocol.h"
#include <QSettings>
#include <QString>
#include <charconv>
#include <future>
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
// Set once a phone did not join the hidden network (see WirelessStation::Impl::ShowWifi).
constexpr const char* kVisibleSetting = "wireless/wifiVisible";
// After a failed start (Bluetooth not up yet at boot, no NetworkManager, ...) the next try comes this much later.
constexpr auto kRetryDelay = 60s;
// How long a phone that opened the Android Auto service waits for a Wi-Fi that is still starting (nmcli takes up to 45 s).
constexpr auto kWifiWait = 60s;

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
    if (const auto hidden = GetEnv("HEADUNIT_WIFI_HIDDEN"); hidden && !hidden->empty()) {
        hotspot.isHidden = !(*hidden == "0" || *hidden == "no" || *hidden == "false");
        settings.isVisibilityFixed = true;
    } else if (QSettings(kSettingsOrganization, kSettingsApplication).value(kVisibleSetting).toBool()) {
        hotspot.isHidden = false;
    }
    return settings;
}

struct WirelessStation::Impl {
    Impl(Logger& log, std::function<void(const std::string&)> status)
        : logger(log), onStatus(std::move(status)), hotspot(log),
          bluetooth(log, [this](const std::string& text) { if (onStatus) onStatus(text); }) {}
    Logger& logger;
    std::function<void(const std::string&)> onStatus;
    WirelessSettings settings{LoadWirelessSettings()};
    Hotspot hotspot;
    BluetoothService bluetooth;
    Socket listener;
    // The Wi-Fi starts in the background (nmcli takes seconds), so that Bluetooth is visible at once and USB is watched
    // meanwhile. `network` is written by that start and read once it has finished.
    std::future<std::string> wifiStart;
    HotspotInfo network;
    WifiCredentials credentials;
    bool isWifiReady{};
    // The causes reported last: a cause that stays is shown once, the retries only go to the log.
    std::string bluetoothError, wifiError;
    std::chrono::steady_clock::time_point nextBluetoothStart{}, nextWifiStart{};

    void Report(const std::string& text)
    {
        logger.Write("INFO", "WLAN", text);
        if (onStatus) onStatus(text);
    }
    void ReportFailure(std::string& last, const std::string& error, const std::string& text)
    {
        if (error != last) Report(text);
        else logger.Write("INFO", "WLAN", text);
        last = error;
    }
    void Stop()
    {
        if (wifiStart.valid()) wifiStart.wait();
        wifiStart = {};
        isWifiReady = false;
        bluetooth.Stop();
        listener.Close();
        hotspot.Stop();
    }
    // Bluetooth comes first and takes a second or two: the phone finds HEATUNIT as soon as the head unit is on.
    void StartBluetooth()
    {
        const auto& name = settings.bluetoothName;
        std::string error = bluetooth.Start(name);
        if (error.empty() && listener.fd < 0) {
            listener.fd = ListenTcp(kWirelessPort, error);
            if (listener.fd < 0 && error.empty()) error = "Port " + std::to_string(kWirelessPort) + " laesst sich nicht oeffnen.";
        }
        if (!error.empty()) {
            bluetooth.Stop();
            nextBluetoothStart = std::chrono::steady_clock::now() + kRetryDelay;
            ReportFailure(bluetoothError, error, "Bluetooth fuer kabelloses Android Auto ist nicht bereit: " + error + " Neuer Versuch in einer Minute; USB geht weiter.");
            return;
        }
        bluetoothError.clear();
        Report("Bluetooth sichtbar als '" + name + "'. Neues Handy: in seinen Bluetooth-Einstellungen " + name + " waehlen und koppeln.");
        if (isWifiReady) bluetooth.ConnectPairedPhones();
    }
    void StartWifi()
    {
        isWifiReady = false;
        const auto config = settings.hotspot;
        logger.Write("INFO", "WLAN", "Starting the Wi-Fi '" + config.ssid + "'" + (config.isHidden ? " (hidden)" : "") + " in the background");
        wifiStart = std::async(std::launch::async, [this, config] {
            HotspotInfo info;
            auto error = StartHotspot(hotspot, config, info, logger);
            if (error.empty()) network = info;
            return error;
        });
    }
    // Takes the result of the background start once it is there, waiting up to `wait` for it.
    void CheckWifi(std::chrono::milliseconds wait = 0ms)
    {
        if (!wifiStart.valid() || wifiStart.wait_for(wait) != std::future_status::ready) return;
        const auto error = wifiStart.get();
        if (!error.empty()) {
            nextWifiStart = std::chrono::steady_clock::now() + kRetryDelay;
            ReportFailure(wifiError, error, "Das WLAN fuer kabelloses Android Auto ist nicht bereit: " + error + " Neuer Versuch in einer Minute; USB geht weiter.");
            return;
        }
        wifiError.clear();
        credentials = {network.ssid, network.password, network.bssid, network.ipAddress, kWirelessPort};
        isWifiReady = true;
        Report("Kabellos bereit: per Bluetooth sichtbar als '" + settings.bluetoothName + "', WLAN '" + network.ssid + "'" +
            (settings.hotspot.isHidden ? " (verborgen)" : "") + " laeuft.");
        // The phones paired before are asked only now, when the Wi-Fi they are sent to is there.
        bluetooth.ConnectPairedPhones();
    }
    // Starts what is not running, when its time has come.
    void Keep()
    {
        const auto now = std::chrono::steady_clock::now();
        if (!bluetooth.IsRunning() && now >= nextBluetoothStart) StartBluetooth();
        CheckWifi();
        if (!isWifiReady && !wifiStart.valid() && now >= nextWifiStart) StartWifi();
    }
    // The phone got the network's details and never joined: Android Auto may not look for a hidden network. From now on
    // (and in later runs) the network's name is broadcast; the phone still joins by itself, nobody has to pick it.
    void ShowWifi()
    {
        settings.hotspot.isHidden = false;
        QSettings(kSettingsOrganization, kSettingsApplication).setValue(kVisibleSetting, true);
        Report("Das Handy ist dem verborgenen WLAN nicht beigetreten. Das WLAN ist ab jetzt sichtbar und startet neu; "
               "das Handy verbindet sich danach wieder von selbst.");
        StartWifi();
    }
};

WirelessStation::WirelessStation(Logger& logger, std::function<void(const std::string&)> onStatus)
    : m_impl(std::make_unique<Impl>(logger, std::move(onStatus))) {}
WirelessStation::~WirelessStation() { m_impl->Stop(); }

void WirelessStation::Start()
{
    auto& impl = *m_impl;
    impl.Stop();
    impl.StartBluetooth();
    impl.StartWifi();
}

int WirelessStation::WaitForPhone(std::chrono::milliseconds timeout)
{
    auto& impl = *m_impl;
    impl.Keep();
    if (!impl.bluetooth.IsRunning()) {
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
    if (!impl.isWifiReady) {
        // The phone came before the Wi-Fi was up (it was quick, or the last start failed): it waits for the start request.
        impl.Report("Handy verbindet sich kabellos; das WLAN startet noch ...");
        if (!impl.wifiStart.valid()) impl.StartWifi();
        const auto deadline = std::chrono::steady_clock::now() + kWifiWait;
        while (!isStopRequested && impl.wifiStart.valid() && std::chrono::steady_clock::now() < deadline) impl.CheckWifi(200ms);
    }
    if (!impl.isWifiReady) {
        result.isStoppedByUser = isStopRequested;
        result.message = "Das WLAN fuer kabelloses Android Auto ist nicht bereit" + (impl.wifiError.empty() ? std::string(".") : ": " + impl.wifiError);
        return result;
    }
    impl.Report("Handy verbindet sich kabellos; es bekommt die WLAN-Daten ueber Bluetooth ...");
    auto link = EstablishWirelessLink(phone.fd, impl.listener.fd, impl.credentials, impl.logger, isStopRequested, 60s);
    if (link.tcpFd < 0) {
        result.isStoppedByUser = isStopRequested;
        result.message = link.message;
        if (link.hasSentInfo && !isStopRequested && impl.settings.hotspot.isHidden) {
            if (impl.settings.isVisibilityFixed) result.message += " Das WLAN ist verborgen (HEADUNIT_WIFI_HIDDEN); mit HEADUNIT_WIFI_HIDDEN=0 sichtbar machen.";
            else impl.ShowWifi();
        }
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
    BluetoothService bluetooth(logger, [](const std::string& text) { std::cout << text << '\n' << std::flush; });
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
