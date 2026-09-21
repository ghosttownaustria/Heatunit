#pragma once
#include "usb/UsbTypes.h"
#include "logging/Logger.h"
#include "androidauto/AndroidAutoSession.h"

namespace headunit {
enum class UsbProbeState { Failed, DriverUnavailable, AoaAvailable, AccessoryAvailable, AccessoryTransportReady, VideoReceived };
struct UsbProbeResult {
    UsbProbeState state{UsbProbeState::Failed};
    std::string stage;
    std::string message;
    // The phone is in its normal USB mode but Windows bound a driver libusb cannot open;
    // rebinding WinUSB (DriverRepair) can fix exactly this. Never set on Linux, where a phone that cannot
    // be opened lacks the udev rule, which only an administrator can install.
    bool canRepairDriver{};
    // The phone did not switch to accessory mode: asking again (after the phone is unlocked or
    // the prompt on it is confirmed) can help.
    bool isRetryable{};
    // Accessory mode was up but Android Auto on the phone never answered: only restarting the
    // phone's USB connection (like a cable replug) helps.
    bool needsRecovery{};
};
// Reads AOA support, or briefly claims/releases an existing accessory interface.
// Does not switch modes or install drivers.
UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger);
// Changes the selected phone's mode and checks its accessory interface, not AA video.
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger);
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger,
    std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
