#include "androidauto/PhoneWatch.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "usb/UsbLogging.h"
#include <algorithm>
#include <exception>
#include <utility>

namespace headunit {
namespace {
using namespace std::chrono_literals;
// One look at the USB bus per round; the wireless wait in between sets the pace.
constexpr auto kRound = 1000ms;
// Rounds after an attempt in which new devices on the bus are only noted: a phone that leaves accessory mode
// enumerates again, and one without a serial number then looks like another device.
constexpr int kSettleRounds = 8;

// The text with a full stop at the end, unless it ends a sentence already.
std::string Sentence(std::string text)
{
    if (!text.empty() && text.back() != '.' && text.back() != '!' && text.back() != '?') text += '.';
    return text;
}
}

// Prepares the watch; nothing happens before Run. `onStep` (may be empty) receives every step for the window.
PhoneWatch::PhoneWatch(PhoneWatchDeps deps, Logger& logger, const std::atomic_bool& isStopRequested, std::atomic_bool& isAttemptStopRequested,
    std::atomic_bool& isUsbRequested, std::function<void(const std::string&)> onStep)
    : m_deps(std::move(deps)), m_logger(logger), m_isStopRequested(isStopRequested), m_isAttemptStopRequested(isAttemptStopRequested),
      m_isUsbRequested(isUsbRequested), m_onStep(std::move(onStep)), m_hasWireless(m_deps.waitForWirelessPhone && m_deps.connectWireless),
      m_readyText(m_hasWireless ? "Bereit: Android Auto startet von selbst, sobald ein Handy per USB angesteckt wird oder sich kabellos verbindet."
                                : "Bereit: Android Auto startet von selbst, sobald ein Handy per USB angesteckt wird.")
{
}

// Watches round by round until `isStopRequested` is set; a failing round is logged and the watch goes on.
AutoConnectResult PhoneWatch::Run()
{
    Step(m_readyText);
    while (!m_isStopRequested) {
        try {
            RunRound();
        } catch (const std::exception& error) {
            m_logger.Write(LogLevel::Error, "WATCH", std::string("Watch round failed: ") + error.what());
            Pause();
        }
    }
    return {false, true, "Automatische Verbindung beendet."};
}

// One round: a look at the USB bus (an attempt for a phone that is new there), then the wait for a wireless phone.
void PhoneWatch::RunRound()
{
    const bool isRequested = m_isUsbRequested.exchange(false);
    if (isRequested) {
        m_knownPhones.clear();
        m_settleRounds = 0;
    }
    if (const auto phones = LookAtUsb()) {
        const bool hasNew = NoteUsbPhones(*phones);
        if (isRequested && phones->empty()) AnswerRequestWithoutPhone();
        if (hasNew) {
            Step("Handy per USB erkannt; starte Android Auto ...");
            Attempt(m_deps.connectUsb, true);
            m_settleRounds = kSettleRounds;
            return;
        }
    }
    if (m_isStopRequested) return;
    if (!m_hasWireless) {
        Pause();
        return;
    }
    const int phone = m_deps.waitForWirelessPhone(kRound);
    if (phone < 0) return;
    Attempt([&] { return m_deps.connectWireless(phone); }, false);
    m_settleRounds = kSettleRounds;
}

// The Android devices on the bus, or nothing when the scan failed. The scan runs every second, so only the first
// failure of a series is worth a log line.
std::optional<std::vector<std::string>> PhoneWatch::LookAtUsb()
{
    try {
        auto phones = m_deps.usbPhones();
        if (m_hasScanError) m_logger.Write(LogLevel::Info, "WATCH", "The USB scan works again");
        m_hasScanError = false;
        return phones;
    } catch (const std::exception& error) {
        if (!m_hasScanError) m_logger.Write(LogLevel::Error, "WATCH", std::string("USB scan failed: ") + error.what());
        m_hasScanError = true;
        return std::nullopt;
    }
}

// Remembers the phones on the bus and tells whether one of them is new. What has left is forgotten, so that plugging
// it in again counts as new; during the settling rounds after an attempt nothing counts as new.
bool PhoneWatch::NoteUsbPhones(const std::vector<std::string>& phones)
{
    bool hasNew = std::any_of(phones.begin(), phones.end(), [this](const std::string& phone) { return !m_knownPhones.contains(phone); });
    m_knownPhones = std::set<std::string>(phones.begin(), phones.end());
    if (m_settleRounds > 0) {
        --m_settleRounds;
        hasNew = false;
    }
    return hasNew;
}

// "Android Auto verbinden" with no phone on the cable: the paired phones are asked to connect wirelessly where that
// exists, otherwise the user is asked for a cable.
void PhoneWatch::AnswerRequestWithoutPhone()
{
    if (!m_hasWireless || !m_deps.requestWireless) {
        Step("Kein Handy am USB-Kabel gefunden. Bitte ein Datenkabel anstecken und das Handy entsperren.");
        return;
    }
    Step(m_deps.requestWireless() ? "Kein Handy am USB-Kabel: verbinde die gekoppelten Handys kabellos (Bluetooth und WLAN) ..."
                                  : "Kein Handy am USB-Kabel, und kabellos ist noch nicht bereit (Bluetooth oder WLAN startet noch).");
}

// Runs one connection attempt and reports how it ended. The attempt's stop flag is cleared first and set again when
// the watch is ending meanwhile (see the class description for why that order is safe).
void PhoneWatch::Attempt(const std::function<AutoConnectResult()>& run, bool isUsb)
{
    m_isAttemptStopRequested = false;
    if (m_isStopRequested) m_isAttemptStopRequested = true;
    if (m_deps.onAttemptStart) m_deps.onAttemptStart();
    AutoConnectResult result;
    try {
        result = run();
    } catch (const std::exception& error) {
        result.message = error.what();
    }
    if (m_deps.onAttemptEnd) m_deps.onAttemptEnd(result);
    const bool hasRun = result.hasVideo || result.isStoppedByUser;
    std::string text = hasRun ? "Android Auto beendet" : isUsb ? "Android Auto per USB kam nicht zustande" : "Kabellos kam keine Verbindung zustande";
    if (!result.message.empty()) text += ": " + result.message;
    text = Sentence(text);
    if (isUsb) text += " Neu verbinden: Kabel neu anstecken oder die Kachel Android Auto waehlen.";
    Step(text);
    if (!m_isStopRequested) Step(m_readyText);
}

// Paces a round where no wireless wait does it.
void PhoneWatch::Pause()
{
    if (m_deps.wait) m_deps.wait(kRound);
}

// Logs a step and hands it to the window.
void PhoneWatch::Step(const std::string& text)
{
    m_logger.Write(LogLevel::Info, "WATCH", text);
    if (m_onStep) m_onStep(text);
}

// How the watch tells the Android devices on the bus apart: by serial number, which a phone keeps when it switches to
// accessory mode and back (its port and product id change then); by port and ids where there is none.
std::vector<std::string> UsbPhoneIdentities(const UsbScanResult& scan)
{
    std::vector<std::string> phones;
    for (const auto& device : scan.devices) {
        if (DetectAndroidDevice(device).evidence == AndroidEvidence::None) continue;
        phones.push_back(IsUsableUsbString(device.serial) ? "serial " + device.serial
                                                          : device.location + " " + UsbIdText(device.vendorId, device.productId));
    }
    return phones;
}

// Runs the watch until `isStopRequested` is set (see PhoneWatch).
AutoConnectResult RunPhoneWatch(const PhoneWatchDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    std::atomic_bool& isAttemptStopRequested, std::atomic_bool& isUsbRequested, const std::function<void(const std::string&)>& onStep)
{
    return PhoneWatch(deps, logger, isStopRequested, isAttemptStopRequested, isUsbRequested, onStep).Run();
}
}
