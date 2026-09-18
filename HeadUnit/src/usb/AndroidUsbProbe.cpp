#include "usb/AndroidUsbProbe.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "androidauto/AoaNegotiator.h"
#include "usb/ProjectionTransport.h"
#include <libusb.h>
#include <memory>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <array>
#include <chrono>
#include <thread>

namespace headunit {
namespace {
struct ContextDeleter { void operator()(libusb_context* context) const { libusb_exit(context); } };
struct ListDeleter { void operator()(libusb_device** devices) const { libusb_free_device_list(devices, 1); } };
struct HandleDeleter { void operator()(libusb_device_handle* handle) const { libusb_close(handle); } };
struct ConfigDeleter { void operator()(libusb_config_descriptor* config) const { libusb_free_config_descriptor(config); } };
std::string Error(int code)
{
    return std::string(libusb_error_name(code)) + " (" + std::to_string(code) + ")";
}
bool IsAccessory(const libusb_device_descriptor& descriptor)
{
    return descriptor.idVendor == 0x18d1 && (descriptor.idProduct == 0x2d00 || descriptor.idProduct == 0x2d01 ||
        descriptor.idProduct == 0x2d04 || descriptor.idProduct == 0x2d05);
}
class LibusbControl final : public IUsbControl {
public:
    explicit LibusbControl(libusb_device_handle* handle) : m_handle(handle) {}
    UsbControlResult Transfer(std::uint8_t requestType, std::uint8_t request,
        std::uint16_t index, std::span<std::uint8_t> bytes) override
    {
        const auto result = libusb_control_transfer(m_handle, requestType, request, 0, index,
            bytes.data(), static_cast<std::uint16_t>(bytes.size()), 2000);
        return {result, result < 0 ? Error(result) : "", result == LIBUSB_ERROR_NO_DEVICE};
    }
private:
    libusb_device_handle* m_handle;
};
using AccessorySession = std::function<UsbProbeResult(libusb_device_handle*, std::uint8_t, std::uint8_t)>;
UsbProbeResult CheckAccessory(libusb_device* device, Logger& logger, const AccessorySession& session)
{
    libusb_device_handle* rawHandle = nullptr;
    auto result = libusb_open(device, &rawHandle);
    if (result < 0) return {UsbProbeState::DriverUnavailable, "Accessory USB open",
        Error(result) + ". Phone is in accessory mode, but its NEW VID/PID needs a usable WinUSB binding. No AA session started."};
    const std::unique_ptr<libusb_device_handle, HandleDeleter> handle(rawHandle);
    libusb_config_descriptor* rawConfig = nullptr;
    result = libusb_get_active_config_descriptor(device, &rawConfig);
    if (result < 0) return {UsbProbeState::Failed, "Accessory descriptors", Error(result)};
    const std::unique_ptr<libusb_config_descriptor, ConfigDeleter> config(rawConfig);
    std::uint8_t input = 0, output = 0;
    for (int i = 0; i < config->bNumInterfaces; ++i) {
        for (int alt = 0; alt < config->interface[i].num_altsetting; ++alt) {
            const auto& descriptor = config->interface[i].altsetting[alt];
            if (descriptor.bInterfaceNumber != 0 || descriptor.bAlternateSetting != 0) continue;
            if (descriptor.bInterfaceClass == 0xff && descriptor.bInterfaceSubClass == 0x42 && descriptor.bInterfaceProtocol == 1) continue;
            for (int ep = 0; ep < descriptor.bNumEndpoints; ++ep) {
                const auto& endpoint = descriptor.endpoint[ep];
                if ((endpoint.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0) input = endpoint.bEndpointAddress;
                else output = endpoint.bEndpointAddress;
            }
        }
    }
    if (!input || !output) return {UsbProbeState::Failed, "Accessory endpoints", "No bulk IN/OUT pair on interface 0, alternate 0"};
    result = libusb_claim_interface(handle.get(), 0);
    if (result < 0) return {UsbProbeState::Failed, "Accessory interface claim", Error(result)};
    std::ostringstream endpoints;
    endpoints << "Accessory interface 0 claimed; bulk IN=0x" << std::hex << static_cast<int>(input)
              << " OUT=0x" << static_cast<int>(output);
    logger.Write("INFO", "USB", endpoints.str());
    UsbProbeResult sessionResult;
    if (session) {
        try { sessionResult = session(handle.get(), input, output); }
        catch (...) { libusb_release_interface(handle.get(), 0); throw; }
    }
    result = libusb_release_interface(handle.get(), 0);
    if (result < 0) return {UsbProbeState::Failed, "Accessory interface release", Error(result)};
    if (session) return sessionResult;
    return {UsbProbeState::AccessoryTransportReady, "Accessory transport",
        endpoints.str() + ". Probe released the interface. Choose Android Auto verbinden to start projection."};
}
}
static UsbProbeResult RunAndroidUsb(const UsbDevice& selected, Logger& logger, bool isStartAccessory, const AccessorySession& session = {})
{
    std::string stage = "device selection";
    const auto finish = [&](UsbProbeState state, const std::string& message) {
        const bool isReady = state == UsbProbeState::AoaAvailable || state == UsbProbeState::AccessoryAvailable || state == UsbProbeState::AccessoryTransportReady || state == UsbProbeState::VideoReceived;
        logger.Write(isReady ? "INFO" : "ERROR", "AA", stage + ": " + message);
        return UsbProbeResult{state, stage, message};
    };
    try {
        if (DetectAndroidDevice(selected).evidence == AndroidEvidence::None)
            return finish(UsbProbeState::Failed, "Selected device has no Android evidence.");
        std::ostringstream identity;
        identity << std::hex << std::uppercase << std::setfill('0') << std::setw(4) << selected.vendorId
                 << ':' << std::setw(4) << selected.productId;
        logger.Write("INFO", "AA", "Checking USB access for " + identity.str() + (isStartAccessory ? "; accessory mode requested" : "; read-only AOA probe"));
        stage = "libusb initialization";
        libusb_context* rawContext = nullptr;
        auto result = libusb_init(&rawContext);
        if (result < 0) return finish(UsbProbeState::Failed, Error(result));
        const std::unique_ptr<libusb_context, ContextDeleter> context(rawContext);
        const auto* version = libusb_get_version();
        logger.Write("DEBUG", "USB", "libusb " + std::to_string(version->major) + "." +
            std::to_string(version->minor) + "." + std::to_string(version->micro));
        stage = "USB enumeration";
        libusb_device** rawDevices = nullptr;
        const auto count = libusb_get_device_list(context.get(), &rawDevices);
        if (count < 0) return finish(UsbProbeState::Failed, Error(static_cast<int>(count)));
        const std::unique_ptr<libusb_device*, ListDeleter> devices(rawDevices);
        libusb_device* target = nullptr;
        libusb_device_descriptor targetDescriptor{};
        int matches = 0;
        for (std::ptrdiff_t index = 0; index < count; ++index) {
            libusb_device_descriptor descriptor{};
            if (libusb_get_device_descriptor(rawDevices[index], &descriptor) != 0) continue;
            if (descriptor.idVendor != selected.vendorId || descriptor.idProduct != selected.productId) continue;
            target = rawDevices[index];
            targetDescriptor = descriptor;
            ++matches;
        }
        if (matches == 0) return finish(UsbProbeState::Failed, "Selected VID/PID is no longer present in libusb. Rescan after reconnecting.");
        if (matches != 1) return finish(UsbProbeState::Failed, "Multiple devices have this VID/PID. Disconnect the other matching devices before probing.");
        stage = "USB open";
        libusb_device_handle* rawHandle = nullptr;
        result = libusb_open(target, &rawHandle);
        if (result < 0) {
            const bool hasDriverIssue = result == LIBUSB_ERROR_NOT_FOUND || result == LIBUSB_ERROR_NOT_SUPPORTED || result == LIBUSB_ERROR_ACCESS;
            return finish(hasDriverIssue ? UsbProbeState::DriverUnavailable : UsbProbeState::Failed,
                Error(result) + ". " + (hasDriverIssue ?
                "Windows exposes the device for discovery, but libusb cannot access it: the phone is bound to the Samsung/MTP driver instead of WinUSB (Windows can silently switch back). Fix: run HeadUnit\\scripts\\Repair-PhoneDriver.ps1 (checks the binding, asks for administrator rights), then rescan. Background: docs/windows_connection.md. No Android Auto handshake was sent." :
                "Device open failed; rescan and inspect the USB connection."));
        }
        std::unique_ptr<libusb_device_handle, HandleDeleter> handle(rawHandle);
        // Confirm identity after opening; VID/PID alone does not distinguish two phones.
        if (targetDescriptor.iSerialNumber != 0 && !selected.serial.empty() && selected.serial.front() != '<') {
            unsigned char serial[256]{};
            const auto length = libusb_get_string_descriptor_ascii(handle.get(), targetDescriptor.iSerialNumber, serial, sizeof(serial));
            if (length < 0) return finish(UsbProbeState::Failed, "Cannot verify selected phone serial: " + Error(length));
            if (std::string(reinterpret_cast<char*>(serial), length) != selected.serial)
                return finish(UsbProbeState::Failed, "Device identity changed. Rescan and select the phone again.");
        }
        logger.Write("INFO", "USB", "Selected device handle opened");
        if (IsAccessory(targetDescriptor)) {
            handle.reset();
            const auto checked = CheckAccessory(target, logger, session);
            stage = checked.stage;
            return finish(checked.state, checked.message);
        }
        stage = "AOA GET_PROTOCOL (51)";
        unsigned char versionBytes[2]{};
        result = libusb_control_transfer(handle.get(), 0xc0, 51, 0, 0, versionBytes, sizeof(versionBytes), 2000);
        if (result < 0) return finish(UsbProbeState::Failed, Error(result) + ". AOA support could not be established; no mode switch attempted.");
        if (result != 2) return finish(UsbProbeState::Failed, "Invalid AOA response length: " + std::to_string(result));
        const auto protocolVersion = static_cast<unsigned>(versionBytes[0] | (versionBytes[1] << 8));
        if (protocolVersion == 0) return finish(UsbProbeState::Failed, "Device reports no AOA support.");
        logger.Write("INFO", "AA", "AOA version " + std::to_string(protocolVersion) + " received from phone");
        if (!isStartAccessory) return finish(UsbProbeState::AoaAvailable,
            "AOA version " + std::to_string(protocolVersion) + " received. Use Start accessory mode for the next USB step. AA video is not connected.");
        std::array<std::uint8_t, 8> ports{};
        const auto portCount = libusb_get_port_numbers(target, ports.data(), static_cast<int>(ports.size()));
        const auto bus = libusb_get_bus_number(target);
        if (portCount <= 0) return finish(UsbProbeState::Failed, "Cannot identify physical USB port; mode change refused");
        LibusbControl control(handle.get());
        RequestAccessoryMode(control, [&](const std::string& nextStage) {
            stage = nextStage;
            logger.Write("INFO", "AA", stage);
        });
        handle.reset();
        stage = "AOA re-enumeration";
        logger.Write("INFO", "USB", "Waiting up to 15 seconds for accessory PID on the same USB port");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (std::chrono::steady_clock::now() < deadline) {
            libusb_device** rawUpdated = nullptr;
            const auto updatedCount = libusb_get_device_list(context.get(), &rawUpdated);
            if (updatedCount < 0) return finish(UsbProbeState::Failed, Error(static_cast<int>(updatedCount)));
            const std::unique_ptr<libusb_device*, ListDeleter> updated(rawUpdated);
            for (std::ptrdiff_t i = 0; i < updatedCount; ++i) {
                libusb_device_descriptor descriptor{};
                if (libusb_get_device_descriptor(rawUpdated[i], &descriptor) != 0 || !IsAccessory(descriptor)) continue;
                std::array<std::uint8_t, 8> currentPorts{};
                const auto currentCount = libusb_get_port_numbers(rawUpdated[i], currentPorts.data(), static_cast<int>(currentPorts.size()));
                if (libusb_get_bus_number(rawUpdated[i]) != bus || currentCount != portCount || currentPorts != ports) continue;
                std::ostringstream message;
                message << "Accessory re-enumerated: VID=18D1 PID=" << std::uppercase << std::hex << descriptor.idProduct;
                logger.Write("INFO", "AA", message.str());
                const auto checked = CheckAccessory(rawUpdated[i], logger, session);
                stage = checked.stage;
                return finish(checked.state, checked.message);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        return finish(UsbProbeState::Failed, "No accessory PID observed on the original USB port within 15 seconds. Check phone prompts and rescan; do not assume a session was established.");
    } catch (const std::exception& error) { return finish(UsbProbeState::Failed, error.what()); }
}
UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger) { return RunAndroidUsb(selected, logger, false); }
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger) { return RunAndroidUsb(selected, logger, true); }
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger,
    std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks) {
    return RunAndroidUsb(selected, logger, true, [&](libusb_device_handle* handle, std::uint8_t input, std::uint8_t output) {
        auto transport = std::make_shared<ProjectionTransport>(handle, input, output);
        const auto result = RunAndroidAutoSession(transport, logger, isStopRequested, callbacks);
        return UsbProbeResult{result.hasVideo ? UsbProbeState::VideoReceived : UsbProbeState::Failed, "Android Auto session", result.message};
    });
}
}
