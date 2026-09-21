#pragma once
#include "logging/Logger.h"
#include "usb/AndroidUsbProbe.h"
#include "usb/DriverRepair.h"
#include "usb/UsbTypes.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <string>

namespace headunit {
// The building blocks of "Android Auto verbinden". Injected so the sequencing can be
// tested without a phone; ConnectPhoneAutomatically supplies the real ones.
struct AutoConnectDeps {
    std::function<UsbScanResult()> scan;
    // Switch to accessory mode (or restart it) and run one Android Auto session.
    std::function<UsbProbeResult(const UsbDevice&)> connect;
    std::function<RepairResult()> repair;
    // Restart the phone's USB connection (like a cable replug) and repair its driver; one elevated step.
    std::function<RepairResult()> recover;
    std::function<void(std::chrono::milliseconds)> wait;
    // Repair and restart show an administrator prompt (Windows UAC) that the user has to confirm. The step
    // messages only ask for that when it exists.
    bool needsAdminPrompt{};
};
struct AutoConnectResult {
    bool hasVideo{};          // a session with real video ran (and has ended by now)
    bool isStoppedByUser{};
    std::string message;
};
// One user action, all steps: find the phone, repair its USB driver binding when the OS
// reverted it (Windows), start Android Auto, and when the phone does not answer restart the USB
// connection and try again. Returns when a session that showed video has ended, the user stopped
// it, or every recovery step has been used up.
AutoConnectResult RunAutoConnect(const AutoConnectDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    const std::function<void(const std::string&)>& onStep);
}
