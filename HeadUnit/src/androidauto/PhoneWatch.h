#pragma once
#include "androidauto/AutoConnect.h"
#include "logging/Logger.h"
#include "usb/UsbTypes.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace headunit {
// The building blocks of the automatic mode, injected like AutoConnectDeps so that the sequencing is tested without a phone.
struct PhoneWatchDeps {
    // The Android devices on the USB bus right now (UsbPhoneIdentities of a quiet scan that writes nothing to the log).
    std::function<std::vector<std::string>()> usbPhones;
    // The whole USB flow for the phone on the bus (accessory mode, driver repair, session): "Android Auto verbinden".
    std::function<AutoConnectResult()> connectUsb;
    // Waits up to the given time for a phone that opened wireless Android Auto over Bluetooth: its RFCOMM socket, or -1.
    // Both wireless functions stay empty where wireless Android Auto is not built.
    std::function<int(std::chrono::milliseconds)> waitForWirelessPhone;
    // The whole wireless flow for that socket (Wi-Fi details, session); it takes the socket over and closes it.
    std::function<AutoConnectResult(int)> connectWireless;
    // "Android Auto verbinden" without a phone on the cable: asks the paired phones to connect wirelessly (a connected one
    // is reconnected, which starts Android Auto on it again). False when wireless is not ready. May stay empty.
    std::function<bool()> requestWireless;
    // Paces the watch where there is no wireless wait.
    std::function<void(std::chrono::milliseconds)> wait;
    // A connection attempt starts and ends (the window shows the phone's picture or the radio's pages).
    std::function<void()> onAttemptStart;
    std::function<void(const AutoConnectResult&)> onAttemptEnd;
};

// "Always ready", like a car: watches USB and Bluetooth at the same time and starts Android Auto for whichever phone
// comes, with no button to press; after the attempt it watches again. A device on USB gets one attempt when it appears
// (also when it is plugged in already at the start) and the next only after it was unplugged, or when `isUsbRequested`
// is set (the "Android Auto verbinden" button). What appears in the first seconds after an attempt is taken as the
// same phone changing its USB identity, not as a new one.
// `isAttemptStopRequested` is what the attempts watch: set, it ends the running attempt only ("Verbindung beenden"); the
// watch clears it before every attempt. `isStopRequested` ends the watch; whoever sets it sets `isAttemptStopRequested`
// right after, so that a running attempt ends too (the order makes the clearing safe).
class PhoneWatch {
public:
    PhoneWatch(PhoneWatchDeps deps, Logger& logger, const std::atomic_bool& isStopRequested, std::atomic_bool& isAttemptStopRequested,
        std::atomic_bool& isUsbRequested, std::function<void(const std::string&)> onStep);

    AutoConnectResult Run();

private:
    PhoneWatchDeps m_deps;
    Logger& m_logger;
    const std::atomic_bool& m_isStopRequested;
    std::atomic_bool& m_isAttemptStopRequested;
    std::atomic_bool& m_isUsbRequested;
    std::function<void(const std::string&)> m_onStep;
    bool m_hasWireless;
    std::string m_readyText;
    std::set<std::string> m_knownPhones;   // the Android devices on the bus that have had their attempt
    int m_settleRounds{};
    bool m_hasScanError{};

    void RunRound();
    std::optional<std::vector<std::string>> LookAtUsb();
    bool NoteUsbPhones(const std::vector<std::string>& phones);
    void AnswerRequestWithoutPhone();
    void Attempt(const std::function<AutoConnectResult()>& run, bool isUsb);
    void Pause();
    void Step(const std::string& text);
};

std::vector<std::string> UsbPhoneIdentities(const UsbScanResult& scan);
AutoConnectResult RunPhoneWatch(const PhoneWatchDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    std::atomic_bool& isAttemptStopRequested, std::atomic_bool& isUsbRequested, const std::function<void(const std::string&)>& onStep);
}
