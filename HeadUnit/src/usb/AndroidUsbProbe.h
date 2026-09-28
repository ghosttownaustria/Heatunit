#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "logging/Logger.h"
#include "usb/UsbProbeResult.h"
#include "usb/UsbTypes.h"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

struct libusb_context;
struct libusb_device;
struct libusb_device_descriptor;
struct libusb_device_handle;

namespace headunit {
// One USB attempt with the selected phone: find it again through libusb, open it and confirm its identity, ask for AOA
// support, switch it to accessory mode (or restart that), wait for its accessory device and check the accessory
// interface or run a session over it. Every step names its stage, so that a failure says where it happened.
class AndroidUsbProbe {
public:
    // Runs on the claimed accessory interface (handle, bulk IN and OUT endpoint) and says how that went.
    using AccessorySession = std::function<UsbProbeResult(libusb_device_handle*, std::uint8_t, std::uint8_t)>;

    AndroidUsbProbe(const UsbDevice& selected, Logger& logger, bool isStartAccessory, AccessorySession session, const std::atomic_bool& isStopRequested);

    UsbProbeResult Run();

private:
    const UsbDevice& m_selected;
    Logger& m_logger;
    bool m_isStartAccessory;
    AccessorySession m_session;
    const std::atomic_bool& m_isStopRequested;
    std::string m_stage{"device selection"};

    UsbProbeResult RunSteps();
    std::optional<UsbProbeResult> FindSelected(libusb_device** devices, std::ptrdiff_t count, libusb_device*& target, libusb_device_descriptor& descriptor);
    UsbProbeResult OpenFailed(int error, const libusb_device_descriptor& descriptor);
    std::optional<UsbProbeResult> VerifySerial(libusb_device_handle* handle, const libusb_device_descriptor& descriptor);
    std::optional<UsbProbeResult> QueryAoaProtocol(libusb_device_handle* handle);
    bool SwitchToAccessory(libusb_device_handle* handle, bool isAlreadyAccessory);
    std::optional<UsbProbeResult> WaitForAccessory(libusb_context* context, bool isRestart);
    UsbProbeResult CheckAccessory(libusb_device* device);
    UsbProbeResult Finish(UsbProbeState state, const std::string& message, bool canRepairDriver = false, bool isRetryable = false, bool needsRecovery = false);
    UsbProbeResult FinishWith(const UsbProbeResult& result);
};

UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger);
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger);
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
