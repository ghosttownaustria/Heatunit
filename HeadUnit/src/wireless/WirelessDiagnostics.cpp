#include "wireless/WirelessDiagnostics.h"
#include "wireless/BluetoothService.h"
#include "wireless/FileDescriptor.h"
#include "wireless/Hotspot.h"
#include "wireless/WirelessLink.h"
#include "wireless/WirelessSettings.h"
#include <atomic>
#include <iostream>
#include <thread>

namespace headunit {
using namespace std::chrono_literals;

// Only the Bluetooth part, for finding out whether a phone pairs and connects: visible under the Bluetooth name, waits
// up to `duration`, and logs what the phone does and says. Returns 0 when a phone opened the service and talked through
// the handshake as far as the Wi-Fi details.
int RunBluetoothTest(Logger& logger, std::chrono::seconds duration)
{
    const auto settings = LoadWirelessSettings();
    BluetoothEvents events;   // no screen: every pairing is confirmed at once
    events.onStatus = [](const std::string& text) { std::cout << text << '\n' << std::flush; };
    BluetoothService bluetooth(logger, events);
    if (const auto error = bluetooth.Start(settings.bluetoothName); !error.empty()) {
        logger.Write(LogLevel::Error, "BT", error);
        std::cerr << error << '\n';
        return 3;
    }
    std::cout << "Bluetooth sichtbar als '" << settings.bluetoothName << "' fuer " << duration.count()
              << " Sekunden. Am Handy in den Bluetooth-Einstellungen koppeln und Android Auto starten; Details in headunit.log.\n"
              << "(Ohne WLAN: das Handy bekommt Platzhalter-Zugangsdaten und kann nicht beitreten. Der Test prueft nur Bluetooth.)\n" << std::flush;
    bluetooth.ConnectPairedPhones();
    const WifiCredentials placeholder{settings.hotspot.ssid, settings.hotspot.password, "00:00:00:00:00:00", "10.42.0.1", kWirelessPort};
    const std::atomic_bool isNeverStopped{false};
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
        const FileDescriptor phone(bluetooth.WaitForPhone(500ms));
        if (!phone.IsValid()) continue;
        const auto link = EstablishWirelessLink(phone.Get(), -1, placeholder, logger, isNeverStopped, 60s);
        logger.Write(link.hasSentInfo ? LogLevel::Info : LogLevel::Error, "TEST", "Bluetooth test: " + link.message);
        if (link.hasSentInfo) {
            std::cout << "Bluetooth-Test bestanden: das Handy hat nach den WLAN-Daten gefragt und sie bekommen.\n";
            return 0;
        }
    }
    logger.Write(LogLevel::Error, "TEST", "Bluetooth test: no phone opened the Android Auto Wireless service");
    std::cerr << "Kein Handy hat den Android-Auto-Dienst geoeffnet (siehe headunit.log: Kopplung? 'Connected'? RFCOMM channel?).\n";
    return 4;
}

// Only the hotspot: starts it, logs and prints how a phone would join, keeps it for `duration`, stops it.
int RunHotspotTest(Logger& logger, std::chrono::seconds duration)
{
    const auto settings = LoadWirelessSettings();
    RemoveLeftoverHotspot(logger);
    Hotspot hotspot(logger);
    HotspotInfo network;
    if (const auto error = hotspot.Start(settings.hotspot, network); !error.empty()) {
        logger.Write(LogLevel::Error, "TEST", error);
        std::cerr << error << '\n';
        return 3;
    }
    std::cout << "Hotspot laeuft: Netz '" << network.ssid << "'" << (settings.hotspot.isHidden ? " (verborgen: am Handy 'Netzwerk hinzufuegen' und den Namen eintippen)" : "")
              << ", Passwort '" << network.password << "', Adresse " << network.ipAddress << ", BSSID " << network.bssid
              << ". Zum Ausprobieren das Handy manuell beitreten lassen (" << duration.count() << " Sekunden).\n" << std::flush;
    std::this_thread::sleep_for(duration);
    return 0;
}
}
