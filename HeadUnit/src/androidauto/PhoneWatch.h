#pragma once
#include "androidauto/AutoConnect.h"
#include "logging/Logger.h"
#include "usb/UsbTypes.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace headunit {
// The building blocks of the automatic mode, injected like AutoConnectDeps so the sequencing is tested without a phone.
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
    // Paces the watch where there is no wireless wait.
    std::function<void(std::chrono::milliseconds)> wait;
    // A connection attempt starts and ends (the window shows the phone's picture or the radio's pages).
    std::function<void()> onAttemptStart;
    std::function<void(const AutoConnectResult&)> onAttemptEnd;
};

// How the watch tells the Android devices on the bus apart: by serial number, which a phone keeps when it switches to
// accessory mode and back (its port and product id change then); by port and ids where there is none.
std::vector<std::string> UsbPhoneIdentities(const UsbScanResult& scan);

// "Always ready", like a car: watches USB and Bluetooth at the same time and starts Android Auto for whichever phone
// comes, with no button to press; after the attempt it watches again. A device on USB gets one attempt when it appears
// (also when it is plugged in already at the start) and the next only after it was unplugged, or when `isUsbRequested`
// is set (the "Android Auto verbinden" button). What appears in the first seconds after an attempt is taken as the
// same phone changing its USB identity, not as a new one.
// `isAttemptStopRequested` is what the attempts watch: set, it ends the running attempt only ("Verbindung beenden"); the
// watch clears it before every attempt. `isStopRequested` ends the watch; whoever sets it sets `isAttemptStopRequested`
// right after, so that a running attempt ends too (the order makes the clearing safe). Returns only then.
AutoConnectResult RunPhoneWatch(const PhoneWatchDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    std::atomic_bool& isAttemptStopRequested, std::atomic_bool& isUsbRequested, const std::function<void(const std::string&)>& onStep);
}
