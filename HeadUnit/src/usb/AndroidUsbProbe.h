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
};
// Reads AOA support, or briefly claims/releases an existing accessory interface.
// Does not switch modes or install drivers.
UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger);
// Changes the selected phone's mode and checks its accessory interface, not AA video.
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger);
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger,
    std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
