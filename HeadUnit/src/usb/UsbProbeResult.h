#pragma once
#include <string>

namespace headunit {
// How far a USB attempt with the phone got.
enum class UsbProbeState { Failed, DriverUnavailable, AoaAvailable, AccessoryTransportReady, VideoReceived };

// The outcome of a probe or connection attempt over USB, and what the caller may do about a failure.
struct UsbProbeResult {
    UsbProbeState state{UsbProbeState::Failed};
    std::string stage;
    std::string message;
    // The phone is in its normal USB mode but Windows bound a driver libusb cannot open; rebinding WinUSB (driver
    // repair) fixes exactly this. Never set on Linux, where a phone that cannot be opened lacks the udev rule.
    bool canRepairDriver{};
    // The phone did not switch to accessory mode: asking again (after it is unlocked or its prompt confirmed) can help.
    bool isRetryable{};
    // Accessory mode was up but Android Auto on the phone never answered: only restarting the phone's USB connection
    // (like a cable replug) helps.
    bool needsRecovery{};
};

bool IsSuccessfulProbe(UsbProbeState state);
}
