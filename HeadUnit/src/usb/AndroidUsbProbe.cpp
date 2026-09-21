#include "usb/AndroidUsbProbe.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "androidauto/AoaNegotiator.h"
#include "usb/ProjectionTransport.h"
#include <libusb.h>
#include <memory>
#include <optional>
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
// What a failed libusb_open of the phone (normal mode) means. `isDriverIssue`: the operating system gave the
// phone a driver or permission set libusb cannot use, as opposed to a plain failure. `canBeRepaired`: the app
// can fix that itself. `advice` says what to do about it.
struct OpenAdvice { bool isDriverIssue; bool canBeRepaired; std::string advice; };
OpenAdvice AdviseOnOpenFailure(int code)
{
#ifdef _WIN32
    const bool isDriverIssue = code == LIBUSB_ERROR_NOT_FOUND || code == LIBUSB_ERROR_NOT_SUPPORTED || code == LIBUSB_ERROR_ACCESS;
    if (!isDriverIssue) return {false, false, "Device open failed; rescan and inspect the USB connection."};
    return {true, true, "Windows exposes the device for discovery, but libusb cannot access it: the phone is bound to the Samsung/MTP driver instead of WinUSB (Windows re-selects the driver whenever the phone re-appears in file-transfer mode). See docs/windows_connection.md. No Android Auto handshake was sent."};
#else
    // Linux: the device node belongs to root until the udev rule grants the user access. Nothing to repair in the app.
    if (code != LIBUSB_ERROR_ACCESS) return {false, false, "Device open failed; rescan and inspect the USB connection."};
    return {true, false, "Keine Berechtigung, das USB-Geraet des Handys zu oeffnen. Einmalig `bash scripts/install-udev-rules.sh` ausfuehren (siehe docs/linux.md) und das Handy danach neu einstecken. Es wurde kein Android-Auto-Handshake gesendet."};
#endif
}
// The same for the phone in accessory mode, which shows up with a new VID/PID (and on Windows needs its own binding).
std::string AccessoryOpenAdvice()
{
#ifdef _WIN32
    return "Phone is in accessory mode, but its NEW VID/PID needs a usable WinUSB binding. No AA session started.";
#else
    return "Das Handy ist im Accessory-Modus, laesst sich aber nicht oeffnen (fehlt die Berechtigung?). Einmalig `bash scripts/install-udev-rules.sh` ausfuehren (siehe docs/linux.md) und das Handy danach neu einstecken. Es wurde keine Android-Auto-Sitzung gestartet.";
#endif
}
using AccessorySession = std::function<UsbProbeResult(libusb_device_handle*, std::uint8_t, std::uint8_t)>;
UsbProbeResult CheckAccessory(libusb_device* device, Logger& logger, const AccessorySession& session)
{
    libusb_device_handle* rawHandle = nullptr;
    auto result = libusb_open(device, &rawHandle);
    if (result < 0) return {UsbProbeState::DriverUnavailable, "Accessory USB open", Error(result) + ". " + AccessoryOpenAdvice()};
    const std::unique_ptr<libusb_device_handle, HandleDeleter> handle(rawHandle);
    // Linux: a kernel driver that grabbed the interface is detached while it is claimed and reattached on release.
    // Other platforms answer NOT_SUPPORTED, which does not matter.
    libusb_set_auto_detach_kernel_driver(handle.get(), 1);
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
bool IsUsableSerial(const std::string& serial) { return !serial.empty() && serial.front() != '<'; }
// The phone's own serial number, read from an already opened handle.
std::optional<std::string> ReadSerial(libusb_device_handle* handle, const libusb_device_descriptor& descriptor) {
    if (descriptor.iSerialNumber == 0) return std::nullopt;
    unsigned char serial[256]{};
    const auto length = libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, serial, sizeof(serial));
    if (length < 0) return std::nullopt;
    return std::string(reinterpret_cast<char*>(serial), static_cast<std::size_t>(length));
}
// The accessory-mode device of the phone with `expectedSerial`. Its location is no guide: in accessory
// mode the phone usually negotiates USB 2.0 instead of SuperSpeed and then appears on another root hub
// port (or even another root hub) than in file-transfer mode. Without a usable serial the accessory
// device is taken only if it is the only one.
libusb_device* FindAccessory(libusb_device** devices, std::ptrdiff_t count, const std::string& expectedSerial, libusb_device_descriptor& found) {
    libusb_device* match = nullptr;
    int candidates = 0;
    for (std::ptrdiff_t i = 0; i < count; ++i) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[i], &descriptor) != 0 || !IsAccessory(descriptor)) continue;
        if (IsUsableSerial(expectedSerial)) {
            libusb_device_handle* rawHandle = nullptr;
            if (libusb_open(devices[i], &rawHandle) < 0) continue;  // driver not ready yet; the caller keeps polling
            const std::unique_ptr<libusb_device_handle, HandleDeleter> handle(rawHandle);
            const auto serial = ReadSerial(handle.get(), descriptor);
            if (!serial || *serial != expectedSerial) continue;
        }
        match = devices[i];
        found = descriptor;
        ++candidates;
    }
    return !IsUsableSerial(expectedSerial) && candidates != 1 ? nullptr : match;
}
// Waits until the phone's accessory-mode device shows up and checks it. A restart has to leave the bus
// first (or give up waiting for that after a few seconds, when the phone restarts Android Auto without
// re-enumerating); a plain switch just waits for the device. A device that appears before its driver
// is ready is retried instead of reported at once.
std::optional<UsbProbeResult> WaitForAccessory(libusb_context* context, const std::string& expectedSerial, std::chrono::seconds timeout,
    bool isRestart, Logger& logger, const AccessorySession& session, const std::atomic_bool* isStopRequested)
{
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + timeout;
    const auto leaveDeadline = begin + std::chrono::milliseconds(2500);
    bool hasLeft = !isRestart;
    std::optional<UsbProbeResult> lastFailure;
    while (std::chrono::steady_clock::now() < deadline) {
        if (isStopRequested && *isStopRequested) return UsbProbeResult{UsbProbeState::Failed, "AOA re-enumeration", "Stopped by user before the phone finished restarting."};
        libusb_device** rawDevices = nullptr;
        const auto count = libusb_get_device_list(context, &rawDevices);
        if (count < 0) return UsbProbeResult{UsbProbeState::Failed, "USB enumeration", Error(static_cast<int>(count))};
        const std::unique_ptr<libusb_device*, ListDeleter> devices(rawDevices);
        libusb_device_descriptor foundDescriptor{};
        libusb_device* found = FindAccessory(rawDevices, count, expectedSerial, foundDescriptor);
        if (!found) hasLeft = true;
        else if (hasLeft || std::chrono::steady_clock::now() >= leaveDeadline) {
            std::ostringstream message;
            message << "Accessory " << (hasLeft ? "re-enumerated" : "restarted in place") << ": VID=18D1 PID=" << std::uppercase << std::hex << foundDescriptor.idProduct
                    << " after " << std::dec << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count()
                    << " ms on bus " << static_cast<int>(libusb_get_bus_number(found)) << " port " << static_cast<int>(libusb_get_port_number(found));
            logger.Write("INFO", "AA", message.str());
            // Android needs a moment to launch Android Auto before it can answer.
            if (!hasLeft) std::this_thread::sleep_for(std::chrono::seconds(1));
            auto checked = CheckAccessory(found, logger, session);
            if (checked.state != UsbProbeState::DriverUnavailable) return checked;
            lastFailure = std::move(checked);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return lastFailure;
}
}
static UsbProbeResult RunAndroidUsb(const UsbDevice& selected, Logger& logger, bool isStartAccessory, const AccessorySession& session = {},
    const std::atomic_bool* isStopRequested = nullptr)
{
    std::string stage = "device selection";
    const auto finish = [&](UsbProbeState state, const std::string& message, bool canRepairDriver = false, bool isRetryable = false, bool needsRecovery = false) {
        const bool isReady = state == UsbProbeState::AoaAvailable || state == UsbProbeState::AccessoryAvailable || state == UsbProbeState::AccessoryTransportReady || state == UsbProbeState::VideoReceived;
        logger.Write(isReady ? "INFO" : "ERROR", "AA", stage + ": " + message);
        return UsbProbeResult{state, stage, message, canRepairDriver, isRetryable, needsRecovery};
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
            const auto advice = AdviseOnOpenFailure(result);
            const bool isNormalMode = !IsAccessory(targetDescriptor);
            return finish(advice.isDriverIssue ? UsbProbeState::DriverUnavailable : UsbProbeState::Failed,
                Error(result) + ". " + advice.advice, advice.canBeRepaired && isNormalMode);
        }
        std::unique_ptr<libusb_device_handle, HandleDeleter> handle(rawHandle);
        // Confirm identity after opening; VID/PID alone does not distinguish two phones.
        if (targetDescriptor.iSerialNumber != 0 && !selected.serial.empty() && selected.serial.front() != '<') {
            unsigned char serial[256]{};
            const auto length = libusb_get_string_descriptor_ascii(handle.get(), targetDescriptor.iSerialNumber, serial, sizeof(serial));
            if (length < 0) {
                // A phone that can be opened but does not answer a descriptor request has a wedged USB link
                // (seen after a long idle time in accessory mode, Windows: "device does not work"). Restarting
                // the link, like unplugging the cable, is the cure.
                const bool isWedged = length == LIBUSB_ERROR_TIMEOUT || length == LIBUSB_ERROR_IO || length == LIBUSB_ERROR_PIPE;
                return finish(UsbProbeState::Failed, "Cannot verify selected phone serial: " + Error(length), false, false, isWedged);
            }
            if (std::string(reinterpret_cast<char*>(serial), length) != selected.serial)
                return finish(UsbProbeState::Failed, "Device identity changed. Rescan and select the phone again.");
        }
        logger.Write("INFO", "USB", "Selected device handle opened");
        const bool isAlreadyAccessory = IsAccessory(targetDescriptor);
        if (isAlreadyAccessory && !session) {
            // Probes only look at the accessory interface; they never restart the phone.
            handle.reset();
            const auto checked = CheckAccessory(target, logger, session);
            stage = checked.stage;
            return finish(checked.state, checked.message);
        }
        if (!isAlreadyAccessory) {
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
        }
        // A finished session leaves the phone in accessory mode with Android Auto stopped, and
        // a new version request then gets no usable answer. Sending the AOA start again makes
        // Android launch Android Auto afresh, so no cable replug (and no driver re-selection
        // by Windows) is needed between sessions.
        bool isRestart = isAlreadyAccessory;
        LibusbControl control(handle.get());
        try {
            RequestAccessoryMode(control, [&](const std::string& nextStage) {
                stage = isRestart ? "AOA restart: " + nextStage : nextStage;
                logger.Write("INFO", "AA", stage);
            });
        } catch (const std::exception& error) {
            if (!isRestart) throw;
            logger.Write("WARN", "AA", std::string("AOA restart not possible (") + error.what() + "); using the running accessory session");
            isRestart = false;
        }
        handle.reset();
        stage = "AOA re-enumeration";
        logger.Write("INFO", "USB", std::string("Waiting up to 30 seconds for the phone's accessory device (found by serial number, its USB port changes)") + (isRestart ? "; restart" : ""));
        const auto waited = WaitForAccessory(context.get(), selected.serial, std::chrono::seconds(30), isRestart, logger, session, isStopRequested);
        if (waited) { stage = waited->stage; return finish(waited->state, waited->message, waited->canRepairDriver, waited->isRetryable, waited->needsRecovery); }
        return finish(UsbProbeState::Failed, "The phone did not switch to Android accessory mode within 30 seconds (locked phone, or Android Auto not allowed to start).", false, true);
    } catch (const std::exception& error) { return finish(UsbProbeState::Failed, error.what()); }
}
UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger) { return RunAndroidUsb(selected, logger, false); }
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger) { return RunAndroidUsb(selected, logger, true); }
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger,
    std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks) {
    return RunAndroidUsb(selected, logger, true, [&](libusb_device_handle* handle, std::uint8_t input, std::uint8_t output) {
        auto transport = std::make_shared<ProjectionTransport>(handle, input, output);
        const auto result = RunAndroidAutoSession(transport, logger, isStopRequested, callbacks);
        UsbProbeResult probe{result.hasVideo ? UsbProbeState::VideoReceived : UsbProbeState::Failed, "Android Auto session", result.message};
        // The accessory connection came up but Android Auto on the phone never answered (and nobody
        // asked to stop): only restarting the phone's USB connection helps.
        probe.needsRecovery = !result.hasVideo && !result.hasVersionReply && !result.isStoppedByUser;
        return probe;
    }, &isStopRequested);
}
}
