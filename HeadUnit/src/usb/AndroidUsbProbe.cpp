#include "usb/AndroidUsbProbe.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "androidauto/AoaNegotiator.h"
#include "usb/DriverRepair.h"
#include "usb/LibusbControl.h"
#include "usb/LibusbHandles.h"
#include "usb/ProjectionTransport.h"
#include "usb/UsbLogging.h"
#include <libusb.h>
#include <chrono>
#include <exception>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr auto kAccessoryWait = 30s;
// How long a restarted accessory gets to leave the bus before it counts as restarted in place.
constexpr auto kLeaveWait = 2500ms;
constexpr unsigned kControlTimeoutMs = 2000;

// The stop flag of runs that cannot be stopped (the probes).
const std::atomic_bool kIsNeverStopped{false};

// The phone's own serial number, read from an already opened handle.
std::optional<std::string> ReadSerial(libusb_device_handle* handle, const libusb_device_descriptor& descriptor)
{
    if (descriptor.iSerialNumber == 0) return std::nullopt;
    unsigned char serial[256]{};
    const auto length = libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, serial, sizeof(serial));
    if (length < 0) return std::nullopt;
    return std::string(reinterpret_cast<char*>(serial), static_cast<std::size_t>(length));
}

// The accessory-mode device of the phone with `expectedSerial`. Its location is no guide: in accessory mode the phone
// usually negotiates USB 2.0 instead of SuperSpeed and then appears on another root hub port (or even another root hub)
// than in file-transfer mode. Without a usable serial the accessory device is taken only if it is the only one.
libusb_device* FindAccessory(libusb_device** devices, std::ptrdiff_t count, const std::string& expectedSerial, libusb_device_descriptor& found)
{
    libusb_device* match = nullptr;
    int candidates = 0;
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        libusb_device_descriptor descriptor{};
        if (libusb_get_device_descriptor(devices[index], &descriptor) != 0 || !IsAccessoryModeId(descriptor.idVendor, descriptor.idProduct)) continue;
        if (IsUsableUsbString(expectedSerial)) {
            libusb_device_handle* rawHandle = nullptr;
            if (libusb_open(devices[index], &rawHandle) < 0) continue;   // driver not ready yet; the caller keeps polling
            const LibusbHandle handle(rawHandle);
            const auto serial = ReadSerial(handle.get(), descriptor);
            if (!serial || *serial != expectedSerial) continue;
        }
        match = devices[index];
        found = descriptor;
        ++candidates;
    }
    return !IsUsableUsbString(expectedSerial) && candidates != 1 ? nullptr : match;
}

// The bulk IN and OUT endpoint of the accessory data interface (interface 0, alternate setting 0, not ADB); 0 where none.
std::pair<std::uint8_t, std::uint8_t> AccessoryEndpoints(const libusb_config_descriptor& config)
{
    std::uint8_t input = 0;
    std::uint8_t output = 0;
    for (int interfaceIndex = 0; interfaceIndex < config.bNumInterfaces; ++interfaceIndex) {
        for (int alternateIndex = 0; alternateIndex < config.interface[interfaceIndex].num_altsetting; ++alternateIndex) {
            const auto& descriptor = config.interface[interfaceIndex].altsetting[alternateIndex];
            if (descriptor.bInterfaceNumber != 0 || descriptor.bAlternateSetting != 0) continue;
            if (descriptor.bInterfaceClass == 0xff && descriptor.bInterfaceSubClass == 0x42 && descriptor.bInterfaceProtocol == 1) continue;
            for (int endpointIndex = 0; endpointIndex < descriptor.bNumEndpoints; ++endpointIndex) {
                const auto& endpoint = descriptor.endpoint[endpointIndex];
                if ((endpoint.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_BULK) continue;
                if ((endpoint.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0) input = endpoint.bEndpointAddress;
                else output = endpoint.bEndpointAddress;
            }
        }
    }
    return {input, output};
}
}

// Prepares one attempt with `selected`. `isStartAccessory`: switch the phone to accessory mode (otherwise only ask for
// AOA support); `session` (may be empty) runs on the accessory interface; `isStopRequested` ends the wait for it.
AndroidUsbProbe::AndroidUsbProbe(const UsbDevice& selected, Logger& logger, bool isStartAccessory, AccessorySession session,
    const std::atomic_bool& isStopRequested)
    : m_selected(selected), m_logger(logger), m_isStartAccessory(isStartAccessory), m_session(std::move(session)), m_isStopRequested(isStopRequested)
{
}

// Runs the attempt; an exception anywhere becomes a failure of the stage it happened in.
UsbProbeResult AndroidUsbProbe::Run()
{
    try {
        return RunSteps();
    } catch (const std::exception& error) {
        return Finish(UsbProbeState::Failed, error.what());
    }
}

// The steps of the attempt, in order; each may end it.
UsbProbeResult AndroidUsbProbe::RunSteps()
{
    if (DetectAndroidDevice(m_selected).evidence == AndroidEvidence::None) return Finish(UsbProbeState::Failed, "Selected device has no Android evidence.");
    m_logger.Write(LogLevel::Info, "AA", "Checking USB access for " + UsbIdText(m_selected.vendorId, m_selected.productId) +
        (m_isStartAccessory ? "; accessory mode requested" : "; read-only AOA probe"));

    m_stage = "libusb initialization";
    libusb_context* rawContext = nullptr;
    if (const int result = libusb_init(&rawContext); result < 0) return Finish(UsbProbeState::Failed, LibusbErrorText(result));
    const LibusbContext context(rawContext);
    const auto* version = libusb_get_version();
    m_logger.Write(LogLevel::Debug, "USB", "libusb " + std::to_string(version->major) + "." + std::to_string(version->minor) + "." +
        std::to_string(version->micro));

    m_stage = "USB enumeration";
    libusb_device** rawDevices = nullptr;
    const auto count = libusb_get_device_list(context.get(), &rawDevices);
    if (count < 0) return Finish(UsbProbeState::Failed, LibusbErrorText(static_cast<int>(count)));
    const LibusbDeviceList devices(rawDevices);
    libusb_device* target = nullptr;
    libusb_device_descriptor targetDescriptor{};
    if (const auto failure = FindSelected(rawDevices, count, target, targetDescriptor)) return *failure;

    m_stage = "USB open";
    libusb_device_handle* rawHandle = nullptr;
    if (const int result = libusb_open(target, &rawHandle); result < 0) return OpenFailed(result, targetDescriptor);
    LibusbHandle handle(rawHandle);
    if (const auto failure = VerifySerial(handle.get(), targetDescriptor)) return *failure;
    m_logger.Write(LogLevel::Info, "USB", "Selected device handle opened");
    const bool isAlreadyAccessory = IsAccessoryModeId(targetDescriptor.idVendor, targetDescriptor.idProduct);
    if (isAlreadyAccessory && !m_session) {
        // Probes only look at the accessory interface; they never restart the phone.
        handle.reset();
        return FinishWith(CheckAccessory(target));
    }
    if (!isAlreadyAccessory) {
        if (const auto answer = QueryAoaProtocol(handle.get())) return *answer;
    }
    const bool isRestart = SwitchToAccessory(handle.get(), isAlreadyAccessory);
    handle.reset();

    m_stage = "AOA re-enumeration";
    m_logger.Write(LogLevel::Info, "USB", std::string("Waiting up to 30 seconds for the phone's accessory device (found by serial number, its USB port changes)") +
        (isRestart ? "; restart" : ""));
    if (const auto waited = WaitForAccessory(context.get(), isRestart)) return FinishWith(*waited);
    return Finish(UsbProbeState::Failed, "The phone did not switch to Android accessory mode within 30 seconds (locked phone, or Android Auto not allowed to start).",
        false, true);
}

// Finds the selected device again by its ids; it must be there exactly once.
std::optional<UsbProbeResult> AndroidUsbProbe::FindSelected(libusb_device** devices, std::ptrdiff_t count, libusb_device*& target,
    libusb_device_descriptor& descriptor)
{
    int matches = 0;
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        libusb_device_descriptor candidate{};
        if (libusb_get_device_descriptor(devices[index], &candidate) != 0) continue;
        if (candidate.idVendor != m_selected.vendorId || candidate.idProduct != m_selected.productId) continue;
        target = devices[index];
        descriptor = candidate;
        ++matches;
    }
    if (matches == 0) return Finish(UsbProbeState::Failed, "Selected VID/PID is no longer present in libusb. Rescan after reconnecting.");
    if (matches != 1) return Finish(UsbProbeState::Failed, "Multiple devices have this VID/PID. Disconnect the other matching devices before probing.");
    return std::nullopt;
}

// The phone cannot be opened: the platform says why, and whether the app can repair it (only in the phone's normal mode).
UsbProbeResult AndroidUsbProbe::OpenFailed(int error, const libusb_device_descriptor& descriptor)
{
    const auto advice = AdviseOnPhoneOpenFailure(error);
    const bool isNormalMode = !IsAccessoryModeId(descriptor.idVendor, descriptor.idProduct);
    return Finish(advice.isDriverIssue ? UsbProbeState::DriverUnavailable : UsbProbeState::Failed, LibusbErrorText(error) + ". " + advice.advice,
        advice.canBeRepaired && isNormalMode);
}

// Confirms the identity after opening, since ids alone do not tell two phones apart. A phone that can be opened but does
// not answer the descriptor request has a wedged USB link (seen after a long idle time in accessory mode, Windows: "device
// does not work"); restarting the link, like unplugging the cable, is the cure.
std::optional<UsbProbeResult> AndroidUsbProbe::VerifySerial(libusb_device_handle* handle, const libusb_device_descriptor& descriptor)
{
    if (descriptor.iSerialNumber == 0 || !IsUsableUsbString(m_selected.serial)) return std::nullopt;
    unsigned char serial[256]{};
    const auto length = libusb_get_string_descriptor_ascii(handle, descriptor.iSerialNumber, serial, sizeof(serial));
    if (length < 0) {
        const bool isWedged = length == LIBUSB_ERROR_TIMEOUT || length == LIBUSB_ERROR_IO || length == LIBUSB_ERROR_PIPE;
        return Finish(UsbProbeState::Failed, "Cannot verify selected phone serial: " + LibusbErrorText(length), false, false, isWedged);
    }
    if (std::string(reinterpret_cast<char*>(serial), static_cast<std::size_t>(length)) != m_selected.serial)
        return Finish(UsbProbeState::Failed, "Device identity changed. Rescan and select the phone again.");
    return std::nullopt;
}

// Asks the phone in its normal mode for its AOA version. Returns the end of the attempt (a failure, or the answer of a
// plain probe); nothing when the attempt goes on to the mode switch.
std::optional<UsbProbeResult> AndroidUsbProbe::QueryAoaProtocol(libusb_device_handle* handle)
{
    m_stage = "AOA GET_PROTOCOL (51)";
    unsigned char versionBytes[2]{};
    const int result = libusb_control_transfer(handle, kAoaVendorIn, kAoaGetProtocol, 0, 0, versionBytes, sizeof(versionBytes), kControlTimeoutMs);
    if (result < 0) return Finish(UsbProbeState::Failed, LibusbErrorText(result) + ". AOA support could not be established; no mode switch attempted.");
    if (result != 2) return Finish(UsbProbeState::Failed, "Invalid AOA response length: " + std::to_string(result));
    const auto protocolVersion = static_cast<unsigned>(versionBytes[0] | (versionBytes[1] << 8));
    if (protocolVersion == 0) return Finish(UsbProbeState::Failed, "Device reports no AOA support.");
    m_logger.Write(LogLevel::Info, "AA", "AOA version " + std::to_string(protocolVersion) + " received from phone");
    if (m_isStartAccessory) return std::nullopt;
    return Finish(UsbProbeState::AoaAvailable,
        "AOA version " + std::to_string(protocolVersion) + " received. Use Start accessory mode for the next USB step. AA video is not connected.");
}

// Sends the AOA identity and START. A finished session leaves the phone in accessory mode with Android Auto stopped, and
// a new version request then gets no usable answer; sending the AOA start again makes Android launch Android Auto
// afresh, so no cable replug (and no driver re-selection by Windows) is needed between sessions. Returns whether the
// phone is being restarted (false when a restart was not possible and the running accessory session is used).
bool AndroidUsbProbe::SwitchToAccessory(libusb_device_handle* handle, bool isAlreadyAccessory)
{
    bool isRestart = isAlreadyAccessory;
    LibusbControl control(handle);
    try {
        RequestAccessoryMode(control, [&](const std::string& nextStage) {
            m_stage = isRestart ? "AOA restart: " + nextStage : nextStage;
            m_logger.Write(LogLevel::Info, "AA", m_stage);
        });
    } catch (const std::exception& error) {
        if (!isRestart) throw;
        m_logger.Write(LogLevel::Warning, "AA", std::string("AOA restart not possible (") + error.what() + "); using the running accessory session");
        isRestart = false;
    }
    return isRestart;
}

// Waits until the phone's accessory-mode device shows up and checks it (or runs the session on it). A restart has to
// leave the bus first (or give up waiting for that after a few seconds, when the phone restarts Android Auto without
// re-enumerating); a plain switch just waits for the device. A device that appears before its driver is ready is retried
// instead of reported at once. Nothing when no accessory device came.
std::optional<UsbProbeResult> AndroidUsbProbe::WaitForAccessory(libusb_context* context, bool isRestart)
{
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + kAccessoryWait;
    const auto leaveDeadline = begin + kLeaveWait;
    bool hasLeft = !isRestart;
    std::optional<UsbProbeResult> lastFailure;
    while (std::chrono::steady_clock::now() < deadline) {
        if (m_isStopRequested) return UsbProbeResult{UsbProbeState::Failed, "AOA re-enumeration", "Stopped by user before the phone finished restarting."};
        libusb_device** rawDevices = nullptr;
        const auto count = libusb_get_device_list(context, &rawDevices);
        if (count < 0) return UsbProbeResult{UsbProbeState::Failed, "USB enumeration", LibusbErrorText(static_cast<int>(count))};
        const LibusbDeviceList devices(rawDevices);
        libusb_device_descriptor foundDescriptor{};
        libusb_device* found = FindAccessory(rawDevices, count, m_selected.serial, foundDescriptor);
        if (!found) {
            hasLeft = true;
        } else if (hasLeft || std::chrono::steady_clock::now() >= leaveDeadline) {
            std::ostringstream message;
            message << "Accessory " << (hasLeft ? "re-enumerated" : "restarted in place") << ": VID=18D1 PID=" << std::uppercase << std::hex
                    << foundDescriptor.idProduct << " after " << std::dec
                    << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() << " ms on bus "
                    << static_cast<int>(libusb_get_bus_number(found)) << " port " << static_cast<int>(libusb_get_port_number(found));
            m_logger.Write(LogLevel::Info, "AA", message.str());
            // Android needs a moment to launch Android Auto before it can answer.
            if (!hasLeft) std::this_thread::sleep_for(1s);
            auto checked = CheckAccessory(found);
            if (checked.state != UsbProbeState::DriverUnavailable) return checked;
            lastFailure = std::move(checked);
        }
        std::this_thread::sleep_for(200ms);
    }
    return lastFailure;
}

// Opens the accessory device and claims its data interface; runs the session there when there is one, otherwise only
// reports that the transport is ready. The interface is released again either way.
UsbProbeResult AndroidUsbProbe::CheckAccessory(libusb_device* device)
{
    libusb_device_handle* rawHandle = nullptr;
    auto result = libusb_open(device, &rawHandle);
    if (result < 0) return {UsbProbeState::DriverUnavailable, "Accessory USB open", LibusbErrorText(result) + ". " + AccessoryOpenAdvice()};
    const LibusbHandle handle(rawHandle);
    // Linux: a kernel driver that grabbed the interface is detached while it is claimed and reattached on release. Other
    // platforms answer NOT_SUPPORTED, which does not matter.
    libusb_set_auto_detach_kernel_driver(handle.get(), 1);
    libusb_config_descriptor* rawConfig = nullptr;
    result = libusb_get_active_config_descriptor(device, &rawConfig);
    if (result < 0) return {UsbProbeState::Failed, "Accessory descriptors", LibusbErrorText(result)};
    const LibusbConfig config(rawConfig);
    const auto [input, output] = AccessoryEndpoints(*config);
    if (!input || !output) return {UsbProbeState::Failed, "Accessory endpoints", "No bulk IN/OUT pair on interface 0, alternate 0"};
    result = libusb_claim_interface(handle.get(), 0);
    if (result < 0) return {UsbProbeState::Failed, "Accessory interface claim", LibusbErrorText(result)};
    std::ostringstream endpoints;
    endpoints << "Accessory interface 0 claimed; bulk IN=0x" << std::hex << static_cast<int>(input) << " OUT=0x" << static_cast<int>(output);
    m_logger.Write(LogLevel::Info, "USB", endpoints.str());
    UsbProbeResult sessionResult;
    if (m_session) {
        try {
            sessionResult = m_session(handle.get(), input, output);
        } catch (...) {
            libusb_release_interface(handle.get(), 0);
            throw;
        }
    }
    result = libusb_release_interface(handle.get(), 0);
    if (result < 0) return {UsbProbeState::Failed, "Accessory interface release", LibusbErrorText(result)};
    if (m_session) return sessionResult;
    return {UsbProbeState::AccessoryTransportReady, "Accessory transport",
        endpoints.str() + ". Probe released the interface. Choose Android Auto verbinden to start projection."};
}

// Ends the attempt in the current stage: logs the outcome and returns it.
UsbProbeResult AndroidUsbProbe::Finish(UsbProbeState state, const std::string& message, bool canRepairDriver, bool isRetryable, bool needsRecovery)
{
    m_logger.Write(IsSuccessfulProbe(state) ? LogLevel::Info : LogLevel::Error, "AA", m_stage + ": " + message);
    return {state, m_stage, message, canRepairDriver, isRetryable, needsRecovery};
}

// Ends the attempt with the result of a step that names its own stage.
UsbProbeResult AndroidUsbProbe::FinishWith(const UsbProbeResult& result)
{
    m_stage = result.stage;
    return Finish(result.state, result.message, result.canRepairDriver, result.isRetryable, result.needsRecovery);
}

// Reads AOA support, or briefly claims and releases an existing accessory interface. Does not switch modes or install
// drivers.
UsbProbeResult ProbeAndroidUsb(const UsbDevice& selected, Logger& logger)
{
    return AndroidUsbProbe(selected, logger, false, {}, kIsNeverStopped).Run();
}

// Switches the phone to accessory mode and checks its accessory interface, without starting Android Auto.
UsbProbeResult StartAndroidAccessory(const UsbDevice& selected, Logger& logger)
{
    return AndroidUsbProbe(selected, logger, true, {}, kIsNeverStopped).Run();
}

// Switches the phone to accessory mode (or restarts it) and runs one Android Auto session over the accessory interface.
UsbProbeResult ConnectAndroidAuto(const UsbDevice& selected, Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
{
    const auto session = [&](libusb_device_handle* handle, std::uint8_t input, std::uint8_t output) {
        auto transport = std::make_shared<ProjectionTransport>(handle, input, output);
        const auto result = RunAndroidAutoSession(transport, logger, isStopRequested, callbacks);
        UsbProbeResult probe{result.hasVideo ? UsbProbeState::VideoReceived : UsbProbeState::Failed, "Android Auto session", result.message};
        // The accessory connection came up but Android Auto on the phone never answered (and nobody asked to stop): only
        // restarting the phone's USB connection helps.
        probe.needsRecovery = !result.hasVideo && !result.hasVersionReply && !result.isStoppedByUser;
        return probe;
    };
    return AndroidUsbProbe(selected, logger, true, session, isStopRequested).Run();
}
}
