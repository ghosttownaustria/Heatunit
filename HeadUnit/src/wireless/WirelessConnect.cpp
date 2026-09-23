#include "wireless/WirelessConnect.h"
#include "platform/Environment.h"
#include "wireless/BluetoothService.h"
#include "wireless/SocketTransport.h"
#include "wireless/WirelessProtocol.h"
#include <QSettings>
#include <QString>
#include <cerrno>
#include <charconv>
#include <iostream>
#include <memory>
#include <poll.h>
#include <random>
#include <sys/socket.h>
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

struct WirelessLink {
    int tcpFd{-1};              // the phone's TCP connection, or -1
    bool hasSentInfo{};         // the phone asked for the Wi-Fi details and got them
    std::string peer;           // the phone's address in the Wi-Fi network
    std::string message;        // why there is no connection
};

// The conversation with the phone over Bluetooth, and the wait for the phone to show up on the Wi-Fi. Success is the
// TCP connection. Without a listening socket (the Bluetooth test) it ends a few seconds after the Wi-Fi details went out.
WirelessLink EstablishWirelessLink(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested, std::chrono::seconds timeout)
{
    WirelessLink link;
    WirelessHandshake handshake(credentials);
    WirelessFrameParser parser;
    const auto sendAll = [&](const std::vector<WirelessMessage>& messages) {
        for (const auto& message : messages) {
            const auto bytes = EncodeWirelessMessage(message);
            std::size_t offset = 0;
            while (offset < bytes.size()) {
                const auto sent = ::send(rfcommFd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
                if (sent < 0 && errno == EINTR) continue;
                if (sent <= 0) return false;
                offset += static_cast<std::size_t>(sent);
            }
        }
        return true;
    };
    const auto step = [&](const WirelessHandshakeStep& result) {
        if (!result.note.empty()) logger.Write("INFO", "WLAN", result.note);
        return sendAll(result.reply);
    };
    if (!step(handshake.Start())) { link.message = "Die Bluetooth-Verbindung zum Handy brach beim Senden ab."; return link; }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::chrono::steady_clock::time_point infoSentAt{};
    while (!isStopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd waiting[2] = {{rfcommFd, POLLIN, 0}, {listenFd, POLLIN, 0}};   // a negative descriptor is skipped by poll
        const int ready = ::poll(waiting, 2, 200);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) { link.message = "Interner Fehler beim Warten auf das Handy."; return link; }
        if (waiting[1].revents & POLLIN) {
            link.tcpFd = AcceptTcp(listenFd, 0, link.peer);
            if (link.tcpFd >= 0) {
                link.hasSentInfo = handshake.HasSentInfo();
                logger.Write("INFO", "WLAN", "The phone opened the wireless connection from " + link.peer);
                return link;
            }
        }
        if (waiting[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            std::uint8_t buffer[1024];
            const auto got = ::recv(rfcommFd, buffer, sizeof(buffer), 0);
            if (got > 0) {
                for (const auto& message : parser.Feed(buffer, static_cast<std::size_t>(got))) {
                    if (!step(handshake.OnMessage(message))) { link.message = "Die Bluetooth-Verbindung zum Handy brach beim Senden ab."; return link; }
                    if (handshake.IsFailed()) { link.message = handshake.Failure() + ". Ist das WLAN am Handy an?"; return link; }
                }
                if (handshake.HasSentInfo() && infoSentAt == std::chrono::steady_clock::time_point{}) infoSentAt = std::chrono::steady_clock::now();
            } else if (got == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                link.hasSentInfo = handshake.HasSentInfo();
                link.message = "Das Handy hat die Bluetooth-Verbindung beendet, bevor es im WLAN war.";
                return link;
            }
        }
        if (listenFd < 0 && infoSentAt != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() > infoSentAt + 5s) {
            link.hasSentInfo = true;
            link.message = "Die WLAN-Daten sind beim Handy angekommen.";
            return link;
        }
    }
    link.hasSentInfo = handshake.HasSentInfo();
    link.message = isStopRequested ? "Abgebrochen." : "Zeitueberschreitung: Das Handy ist dem WLAN nicht beigetreten.";
    return link;
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

    report("Starte den WLAN-Hotspot '" + settings.hotspot.ssid + "' (der WLAN-Chip wird dabei zum Hotspot: eine SSH-Verbindung ueber WLAN bricht ab, Ethernet bleibt).");
    Hotspot hotspot(logger);
    HotspotInfo network;
    if (const auto error = hotspot.Start(settings.hotspot, network); !error.empty()) { result.message = error; return result; }

    std::string listenError;
    Socket listener(ListenTcp(kWirelessPort, listenError));
    if (listener.fd < 0) { result.message = listenError; return result; }

    BluetoothService bluetooth(logger);
    if (const auto error = bluetooth.Start(settings.bluetoothName); !error.empty()) { result.message = error; return result; }
    report("Bereit. Erstes Mal: am Handy in den Bluetooth-Einstellungen '" + settings.bluetoothName + "' koppeln. Danach verbindet sich das Handy von selbst.");

    const WifiCredentials credentials{network.ssid, network.password, network.bssid, network.ipAddress, kWirelessPort};
    while (!isStopRequested) {
        Socket phone(bluetooth.WaitForPhone(500ms));
        if (phone.fd < 0) continue;
        report("Handy per Bluetooth verbunden; uebergebe die WLAN-Daten ...");
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
              << " Sekunden. Am Handy in den Bluetooth-Einstellungen koppeln und Android Auto starten; Details in headunit.log.\n" << std::flush;
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
