#include "androidauto/PhoneWatch.h"
#include "androidauto/AndroidDeviceDetector.h"
#include <cstdio>
#include <exception>
#include <set>

namespace headunit {
namespace {
using namespace std::chrono_literals;
// One look at the USB bus per round; the wireless wait in between sets the pace.
constexpr auto kRound = 1000ms;
// Rounds after an attempt in which new devices on the bus are only noted: a phone that leaves accessory mode
// enumerates again, and one without a serial number then looks like another device.
constexpr int kSettleRounds = 8;
// "<not supplied>" and "<unavailable>" stand in for strings the scan could not read.
bool IsUsable(const std::string& serial) { return !serial.empty() && serial.front() != '<'; }
std::string Hex4(std::uint16_t value)
{
    char text[8]{};
    std::snprintf(text, sizeof(text), "%04X", static_cast<unsigned>(value));
    return text;
}
std::string Sentence(std::string text)
{
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') text += '.';
    return text;
}
}

std::vector<std::string> UsbPhoneIdentities(const UsbScanResult& scan)
{
    std::vector<std::string> phones;
    for (const auto& device : scan.devices) {
        if (DetectAndroidDevice(device).evidence == AndroidEvidence::None) continue;
        phones.push_back(IsUsable(device.serial) ? "serial " + device.serial
                                                 : device.location + " " + Hex4(device.vendorId) + ":" + Hex4(device.productId));
    }
    return phones;
}

AutoConnectResult RunPhoneWatch(const PhoneWatchDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    std::atomic_bool& isAttemptStopRequested, std::atomic_bool& isUsbRequested, const std::function<void(const std::string&)>& onStep)
{
    const auto step = [&](const std::string& text) { logger.Write("INFO", "WATCH", text); if (onStep) onStep(text); };
    const bool hasWireless = deps.waitForWirelessPhone && deps.connectWireless;
    const std::string ready = hasWireless
        ? "Bereit: Android Auto startet von selbst, sobald ein Handy per USB angesteckt wird oder sich kabellos verbindet."
        : "Bereit: Android Auto startet von selbst, sobald ein Handy per USB angesteckt wird.";
    const auto attempt = [&](const std::function<AutoConnectResult()>& run, bool isUsb) {
        // Cleared for this attempt, and set again when the watch is ending meanwhile (the order is explained in the header).
        isAttemptStopRequested = false;
        if (isStopRequested) isAttemptStopRequested = true;
        if (deps.onAttemptStart) deps.onAttemptStart();
        AutoConnectResult result;
        try { result = run(); }
        catch (const std::exception& error) { result.message = error.what(); }
        if (deps.onAttemptEnd) deps.onAttemptEnd(result);
        const bool hasRun = result.hasVideo || result.isStoppedByUser;
        std::string text = hasRun ? "Android Auto beendet" : isUsb ? "Android Auto per USB kam nicht zustande" : "Kabellos kam keine Verbindung zustande";
        if (!result.message.empty()) text += ": " + result.message;
        text = Sentence(text);
        if (isUsb) text += " Neu verbinden: Kabel neu anstecken oder auf Android Auto verbinden druecken.";
        step(text);
        if (!isStopRequested) step(ready);
    };
    const auto pause = [&] { if (deps.wait) deps.wait(kRound); };

    step(ready);
    std::set<std::string> known;   // the Android devices on the bus that have had their attempt
    int settleRounds = 0;
    bool hasScanError = false;
    while (!isStopRequested) {
        try {
            const bool isRequested = isUsbRequested.exchange(false);
            if (isRequested) { known.clear(); settleRounds = 0; }
            std::vector<std::string> phones;
            bool hasScanned = false;
            try {
                phones = deps.usbPhones();
                hasScanned = true;
                if (hasScanError) logger.Write("INFO", "WATCH", "The USB scan works again");
                hasScanError = false;
            } catch (const std::exception& error) {
                // Every second: only the first failure is worth a line.
                if (!hasScanError) logger.Write("ERROR", "WATCH", std::string("USB scan failed: ") + error.what());
                hasScanError = true;
            }
            if (hasScanned) {
                bool hasNew = false;
                for (const auto& phone : phones) hasNew = hasNew || !known.contains(phone);
                // What has left is forgotten, so that plugging it in again counts as new.
                known = std::set<std::string>(phones.begin(), phones.end());
                if (settleRounds > 0) { --settleRounds; hasNew = false; }
                if (isRequested && phones.empty()) step("Kein Handy am USB-Kabel gefunden. Bitte ein Datenkabel anstecken und das Handy entsperren.");
                if (hasNew) {
                    step("Handy per USB erkannt; starte Android Auto ...");
                    attempt(deps.connectUsb, true);
                    settleRounds = kSettleRounds;
                    continue;
                }
            }
            if (isStopRequested) break;
            if (!hasWireless) { pause(); continue; }
            const int phone = deps.waitForWirelessPhone(kRound);
            if (phone < 0) continue;
            attempt([&] { return deps.connectWireless(phone); }, false);
            settleRounds = kSettleRounds;
        } catch (const std::exception& error) {
            logger.Write("ERROR", "WATCH", std::string("Watch round failed: ") + error.what());
            pause();
        }
    }
    AutoConnectResult outcome;
    outcome.isStoppedByUser = true;
    outcome.message = "Automatische Verbindung beendet.";
    return outcome;
}
}
