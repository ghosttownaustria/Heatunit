#include "androidauto/AutoConnect.h"
#include "androidauto/AndroidDeviceDetector.h"
#include <optional>
#include <sstream>

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr int kFindTries = 10;            // one scan per second while the phone is missing
constexpr int kFindTriesAfterRecovery = 25;  // a restarted phone needs time to enumerate and load drivers
constexpr int kMaxRecoveries = 2;
constexpr int kMaxDriverRepairs = 3;
constexpr int kMaxRounds = 10;
std::string Describe(const UsbDevice& device) {
    std::ostringstream text;
    text << std::hex << std::uppercase;
    text.fill('0');
    text.width(4); text << device.vendorId << ':';
    text.width(4); text << device.productId;
    return text.str() + (device.product.empty() ? "" : " " + device.product);
}
bool IsAccessoryMode(const UsbDevice& device) { return DetectAndroidDevice(device).evidence == AndroidEvidence::AccessoryMode; }
}

AutoConnectResult RunAutoConnect(const AutoConnectDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    const std::function<void(const std::string&)>& onStep)
{
    AutoConnectResult outcome;
    const auto step = [&](const std::string& text) { logger.Write("INFO", "AUTO", text); if (onStep) onStep(text); };
    const auto stopped = [&] { outcome.isStoppedByUser = true; outcome.message = "Verbindung beendet."; return outcome; };
    const auto failed = [&](std::string message) { logger.Write("ERROR", "AUTO", message); outcome.message = std::move(message); return outcome; };
    int recoveries = 0, repairs = 0, findTries = kFindTries;
    for (int round = 0; round < kMaxRounds; ++round) {
        if (isStopRequested) return stopped();
        step(round == 0 ? "Suche Android-Handy ..." : "Suche das Handy erneut ...");
        std::optional<UsbDevice> phone;
        for (int attempt = 0; attempt < findTries && !phone && !isStopRequested; ++attempt) {
            UsbScanResult scan;
            try { scan = deps.scan(); } catch (const std::exception& error) { logger.Write("ERROR", "AUTO", error.what()); }
            int candidates = 0;
            for (const auto& device : scan.devices)
                if (DetectAndroidDevice(device).evidence != AndroidEvidence::None) { phone = device; ++candidates; }
            if (candidates > 1) return failed("Mehrere Android-Geraete gefunden. Bitte nur ein Handy anschliessen.");
            if (candidates == 0) deps.wait(1000ms);
        }
        if (isStopRequested) return stopped();
        if (!phone) return failed("Kein Android-Handy gefunden. Bitte ein Datenkabel anschliessen und das Handy entsperren.");
        findTries = kFindTries;
        step("Handy gefunden: " + Describe(*phone) + (IsAccessoryMode(*phone) ? " (Android-Auto-Modus, wird neu gestartet)" : ""));

        const auto result = deps.connect(*phone);
        if (result.state == UsbProbeState::VideoReceived) { outcome.hasVideo = true; outcome.message = result.message; return outcome; }
        if (isStopRequested) return stopped();

        if (result.canRepairDriver) {
            // Windows gave the phone Samsung's driver again; WinUSB is needed to talk to it.
            if (repairs >= kMaxDriverRepairs) return failed("Der USB-Treiber des Handys laesst sich nicht dauerhaft reparieren: " + result.message);
            ++repairs;
            step("Windows hat dem Handy den falschen USB-Treiber zugewiesen. Repariere ihn - bitte die Windows-Abfrage (Administratorrechte) bestaetigen ...");
            const auto repaired = deps.repair();
            if (!repaired.IsUsable()) return failed(repaired.message);
            step(repaired.message);
            deps.wait(2000ms);
            continue;
        }
        if (result.isRetryable && recoveries < kMaxRecoveries) {
            // Same effect as unplugging and replugging the cable, which is what fixes a phone
            // whose Android Auto does not start.
            ++recoveries;
            step("Das Handy startet Android Auto nicht (" + result.message + "). Starte die USB-Verbindung neu (" +
                std::to_string(recoveries) + "/" + std::to_string(kMaxRecoveries) + ") - bitte die Windows-Abfrage (Administratorrechte) bestaetigen ...");
            const auto recovered = deps.recover();
            if (!recovered.IsUsable()) return failed(recovered.message + " Alternativ das Kabel einmal abziehen und wieder anstecken.");
            step(recovered.message);
            deps.wait(2000ms);
            findTries = kFindTriesAfterRecovery;
            continue;
        }
        if (result.isRetryable)
            return failed("Android Auto startet auf dem Handy nicht: " + result.message + " Bitte das Handy entsperren, pruefen ob Android Auto aktiviert ist, und erneut verbinden.");
        return failed(result.stage + ": " + result.message);
    }
    return failed("Die Verbindung kam nach mehreren Versuchen nicht zustande.");
}
}
