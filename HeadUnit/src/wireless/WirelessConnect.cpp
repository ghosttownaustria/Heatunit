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

// Closes a socket when it goes out of scope.
struct Socket {
    explicit Socket(int descriptor = -1) : fd(descriptor) {}
    ~Socket() { if (fd >= 0) ::close(fd); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
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
    return settings;
}

AutoConnectResult ConnectWirelessAndroidAuto(Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
{
    AutoConnectResult result;
    const auto report = [&](const std::string& text) {
        logger.Write("INFO", "WLAN", text);
        if (callbacks.onStatus) callbacks.onStatus(text);
    };
    const auto settings = LoadWirelessSettings();
    RemoveLeftoverHotspot(logger);

    // The Wi-Fi comes first and stays up for the whole wireless mode. The phone expects the start request as soon as it
    // has opened the Android Auto service; a hotspot started only then kept it waiting for seconds. The dongles and
    // openauto that work with real phones have their access point up before Bluetooth, too.
    report("Starte das WLAN '" + settings.hotspot.ssid + "' (einige Sekunden) ...");
    Hotspot hotspot(logger);
    HotspotInfo network;
    if (const auto error = StartHotspot(hotspot, settings.hotspot, network, logger); !error.empty()) { result.message = error; return result; }
    const WifiCredentials credentials{network.ssid, network.password, network.bssid, network.ipAddress, kWirelessPort};

    report("Schalte Bluetooth ein ...");
    BluetoothService bluetooth(logger);
    if (const auto error = bluetooth.Start(settings.bluetoothName); !error.empty()) { result.message = error; return result; }
    std::string listenError;
    Socket listener(ListenTcp(kWirelessPort, listenError));
    if (listener.fd < 0) { result.message = listenError; return result; }
    report("Bereit: WLAN '" + network.ssid + "' laeuft, per Bluetooth sichtbar als '" + settings.bluetoothName + "'. "
        "Erstes Mal: am Handy in den Bluetooth-Einstellungen koppeln, danach verbindet sich das Handy von selbst.");
    bluetooth.ConnectPairedPhones();

    while (!isStopRequested) {
        Socket phone(bluetooth.WaitForPhone(500ms));
        if (phone.fd < 0) continue;
        report("Handy baut Android Auto auf; es bekommt die WLAN-Daten ueber Bluetooth ...");
        auto link = EstablishWirelessLink(phone.fd, listener.fd, credentials, logger, isStopRequested, 60s);
        if (link.tcpFd < 0) {
            if (isStopRequested) break;
            report(link.message + " Warte erneut auf das Handy.");
            continue;
        }
        report("Handy im WLAN verbunden (" + link.peer + "); starte Android Auto.");
        // The Bluetooth link stays open until the session is over: the phone treats it as the car being connected.
        const auto session = RunAndroidAutoSession(std::make_shared<SocketTransport>(link.tcpFd), logger, isStopRequested, callbacks);
        result.hasVideo = session.hasVideo;
        result.isStoppedByUser = session.isStoppedByUser;
        result.message = session.message;
        if (session.hasVideo || session.isStoppedByUser) return result;
        report("Die Sitzung endete ohne Bild (" + session.message + "). Warte erneut auf das Handy.");
    }
    result.isStoppedByUser = true;
    result.message = "Kabellose Verbindung beendet.";
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
    std::cerr << "Kein Handy hat den Android-Auto-Dienst geoeffnet (siehe headunit.log: Kopplung? 'Connected'?).\n";
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
    std::cout << "Hotspot laeuft: Netz '" << network.ssid << "', Passwort '" << network.password << "', Adresse " << network.ipAddress
              << ", BSSID " << network.bssid << ". Zum Ausprobieren das Handy manuell beitreten lassen (" << duration.count() << " Sekunden).\n" << std::flush;
    std::this_thread::sleep_for(duration);
    return 0;
}
}
