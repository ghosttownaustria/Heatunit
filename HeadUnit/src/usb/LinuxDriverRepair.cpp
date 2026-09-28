// Linux implementation of DriverRepair.h. There is no driver to repair: the phone's USB device node only has to be
// accessible to the user, which the udev rule of packaging/linux/70-headunit-android.rules arranges. The app can detect
// that it is missing, not fix it.
#include "usb/DriverRepair.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "usb/LibusbHandles.h"
#include "usb/LibusbUsbBackend.h"
#include <chrono>
#include <cstddef>
#include <fstream>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr int kMaxRestartAttempts = 3;
constexpr const char* kAccessAdvice = "Keine Berechtigung fuer das USB-Geraet des Handys. Einmalig als Administrator "
    "`bash scripts/install-udev-rules.sh` ausfuehren (siehe docs/linux.md) und das Handy danach neu einstecken.";

// An Android device on the bus, and whether it is in accessory mode.
struct Phone {
    UsbDevice info;
    bool isAccessory{};
};

// Where the phone is.
enum class Mode { Gone, Normal, Accessory };

// A device opened by its location. The handle is declared after the context, so it is closed first.
struct OpenedDevice {
    LibusbContext context;
    LibusbHandle handle;
    int error{LIBUSB_ERROR_NO_DEVICE};
};

// The outcome of one restart of the phone's USB link.
struct Restart {
    bool isDone{};
    int error{};
};

// The Android devices on the bus right now; a scan without log lines, because callers poll.
std::vector<Phone> FindPhones(Logger& logger)
{
    LibusbUsbBackend backend(logger, true);
    auto scan = backend.EnumerateDevices();
    std::vector<Phone> phones;
    for (auto& device : scan.devices) {
        const auto detection = DetectAndroidDevice(device);
        if (detection.evidence == AndroidEvidence::None) continue;
        phones.push_back({std::move(device), detection.evidence == AndroidEvidence::AccessoryMode});
    }
    return phones;
}

// The phone with `serial` (the first phone when there is no usable serial). Its location is no guide: in accessory
// mode it usually shows up on another port.
const Phone* Match(const std::vector<Phone>& phones, const std::string& serial)
{
    for (const auto& phone : phones) {
        if (!IsUsableUsbString(serial) || phone.info.serial == serial) return &phone;
    }
    return nullptr;
}

// Where the phone with `serial` is.
Mode ModeOf(const std::vector<Phone>& phones, const std::string& serial)
{
    const Phone* phone = Match(phones, serial);
    return !phone ? Mode::Gone : phone->isAccessory ? Mode::Accessory : Mode::Normal;
}

// Opens the device at `location`; the handle stays empty on failure, with the libusb error in `error`.
OpenedDevice Open(const std::string& location)
{
    OpenedDevice opened;
    libusb_context* rawContext = nullptr;
    if ((opened.error = libusb_init(&rawContext)) < 0) return opened;
    opened.context.reset(rawContext);
    libusb_device** rawList = nullptr;
    const auto count = libusb_get_device_list(rawContext, &rawList);
    if (count < 0) {
        opened.error = static_cast<int>(count);
        return opened;
    }
    const LibusbDeviceList list(rawList);
    opened.error = LIBUSB_ERROR_NO_DEVICE;
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        if (LibusbLocation(rawList[index]) != location) continue;
        libusb_device_handle* rawHandle = nullptr;
        opened.error = libusb_open(rawList[index], &rawHandle);
        if (opened.error == 0) opened.handle.reset(rawHandle);
        break;
    }
    return opened;
}

#ifdef __linux__
// Writes `value` into a sysfs attribute; false when that is not allowed.
bool WriteSysfs(const std::string& path, const char* value)
{
    std::ofstream file(path);
    if (!file) return false;
    file << value;
    file.flush();
    return static_cast<bool>(file);
}

// Takes the device away from the bus for `offTime` (the kernel unconfigures and releases it) and gives it back.
// Writing the attribute needs administrator rights, so this is skipped for a normal user.
bool CycleAuthorization(const std::string& location, std::chrono::milliseconds offTime, Logger& logger)
{
    if (location.rfind("usb:", 0) != 0) return false;
    const std::string attribute = "/sys/bus/usb/devices/" + location.substr(4) + "/authorized";
    if (!WriteSysfs(attribute, "0")) return false;
    logger.Write(LogLevel::Info, "REPAIR", "Phone deauthorized through " + attribute + " for " + std::to_string(offTime.count()) + " ms");
    std::this_thread::sleep_for(offTime);
    if (!WriteSysfs(attribute, "1")) logger.Write(LogLevel::Error, "REPAIR", "The phone could not be authorized again: " + attribute + " (unplug and replug it)");
    return true;
}
#endif

// Restarts the phone's USB link. The first attempt is a libusb port reset, which needs no administrator rights; whether
// a phone treats it like a cable replug depends on the phone. Later attempts take the phone off the bus for longer
// through sysfs where the user may do that (Windows needed the same escalation).
Restart RestartLink(const Phone& phone, int attempt, std::chrono::milliseconds offTime, Logger& logger)
{
#ifdef __linux__
    if (attempt > 1 && CycleAuthorization(phone.info.location, offTime, logger)) return {true, 0};
#else
    (void)attempt;
    (void)offTime;
#endif
    const auto opened = Open(phone.info.location);
    if (!opened.handle) return {false, opened.error};
    const int code = libusb_reset_device(opened.handle.get());
    logger.Write(LogLevel::Info, "REPAIR", "libusb device reset of " + phone.info.location + ": " + LibusbErrorText(code));
    // NOT_FOUND / NO_DEVICE: the phone re-enumerated under a new address, which is exactly what was asked for.
    return {code == 0 || code == LIBUSB_ERROR_NOT_FOUND || code == LIBUSB_ERROR_NO_DEVICE, code};
}

// Waits for the phone to come back after a restart; what it returns as is the answer. A phone that never left the bus
// did not notice the restart: that attempt failed, so it is not waited out.
Mode WaitForPhone(const std::string& serial, Logger& logger)
{
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + 25s;
    bool hasLeft = false;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(500ms);
        const Mode mode = ModeOf(FindPhones(logger), serial);
        if (mode == Mode::Gone) hasLeft = true;
        else if (mode == Mode::Normal || hasLeft) return mode;
        else if (std::chrono::steady_clock::now() - begin > 9s) break;
    }
    return ModeOf(FindPhones(logger), serial);
}

// Why the phones found cannot be worked on, or nothing when there is exactly one.
std::optional<RepairResult> NotExactlyOne(const std::vector<Phone>& phones)
{
    if (phones.empty()) return RepairResult{RepairOutcome::NoPhone, "Kein Android-Handy per USB gefunden. Kabel und Entsperrung pruefen."};
    if (phones.size() > 1) return RepairResult{RepairOutcome::Failed, "Mehrere Android-Geraete gefunden. Bitte nur ein Handy anschliessen."};
    return std::nullopt;
}
}

// Checks that the phone can be opened and says what to install when it cannot.
RepairResult RepairPhoneDriverNow(Logger& logger)
{
    const auto phones = FindPhones(logger);
    if (const auto problem = NotExactlyOne(phones)) return *problem;
    const Phone& phone = phones.front();
    const auto opened = Open(phone.info.location);
    if (opened.handle) {
        if (phone.isAccessory) return {RepairOutcome::InAccessoryMode, "Handy ist im Accessory-Modus und laesst sich oeffnen; es ist nichts zu tun."};
        return {RepairOutcome::AlreadyOk, "Der Zugriff auf das Handy funktioniert; es ist nichts zu reparieren."};
    }
    logger.Write(LogLevel::Error, "REPAIR", "Cannot open " + phone.info.location + ": " + LibusbErrorText(opened.error));
    if (opened.error == LIBUSB_ERROR_ACCESS) return {RepairOutcome::AccessDenied, kAccessAdvice};
    return {RepairOutcome::Failed, "Das Handy laesst sich nicht oeffnen: " + LibusbErrorText(opened.error)};
}

// Restarts the phone's USB link until it is back in its normal mode, best effort: a libusb device reset, then sysfs
// (needs write access to it). The phone only leaves accessory mode when it sees the link go away for long enough, so the
// time grows per attempt.
RepairResult RecoverPhoneNow(Logger& logger)
{
    auto phones = FindPhones(logger);
    if (const auto problem = NotExactlyOne(phones)) return *problem;
    Phone phone = phones.front();
    const std::string serial = phone.info.serial;
    for (int attempt = 1; attempt <= kMaxRestartAttempts; ++attempt) {
        const auto offTime = 3000ms + 2500ms * (attempt - 1);
        logger.Write(LogLevel::Info, "REPAIR", "Restarting the phone's USB link (attempt " + std::to_string(attempt) + ")");
        const auto restart = RestartLink(phone, attempt, offTime, logger);
        if (!restart.isDone) {
            logger.Write(LogLevel::Error, "REPAIR", "USB restart failed: " + LibusbErrorText(restart.error));
            if (restart.error == LIBUSB_ERROR_ACCESS) return {RepairOutcome::AccessDenied, kAccessAdvice};
            return {RepairOutcome::Failed, "Der USB-Neustart des Handys ist fehlgeschlagen: " + LibusbErrorText(restart.error)};
        }
        if (WaitForPhone(serial, logger) == Mode::Normal) {
            logger.Write(LogLevel::Info, "REPAIR", "Phone is back in normal mode");
            std::this_thread::sleep_for(2s);   // let the system finish setting the device up first
            return {RepairOutcome::Fixed, "Handy neu gestartet."};
        }
        phones = FindPhones(logger);
        const Phone* again = Match(phones, serial);
        if (!again) break;
        phone = *again;
    }
    logger.Write(LogLevel::Error, "REPAIR", "Phone did not come back in normal mode after the USB restart");
    return {RepairOutcome::Timeout, "Das Handy hat sich nach dem USB-Neustart nicht im normalen Modus zurueckgemeldet. Bitte das Kabel einmal abziehen und wieder anstecken."};
}

// Nothing needs elevation here: what the user may do, the app may do.
RepairResult RepairPhoneDriverElevated(Logger& logger)
{
    return RepairPhoneDriverNow(logger);
}

// Nothing needs elevation here: what the user may do, the app may do.
RepairResult RecoverPhoneElevated(Logger& logger)
{
    return RecoverPhoneNow(logger);
}

// Linux has no prompt: the steps never ask the user to confirm one.
bool NeedsAdminPromptForRepair()
{
    return false;
}

// The device node belongs to root until the udev rule grants the user access; nothing to repair in the app.
UsbOpenAdvice AdviseOnPhoneOpenFailure(int libusbError)
{
    if (libusbError != LIBUSB_ERROR_ACCESS) return {false, false, "Device open failed; rescan and inspect the USB connection."};
    return {true, false,
        "Keine Berechtigung, das USB-Geraet des Handys zu oeffnen. Einmalig `bash scripts/install-udev-rules.sh` ausfuehren (siehe docs/linux.md) "
        "und das Handy danach neu einstecken. Es wurde kein Android-Auto-Handshake gesendet."};
}

// The phone in accessory mode that cannot be opened most likely misses the permission, as in normal mode.
std::string AccessoryOpenAdvice()
{
    return "Das Handy ist im Accessory-Modus, laesst sich aber nicht oeffnen (fehlt die Berechtigung?). Einmalig `bash scripts/install-udev-rules.sh` "
        "ausfuehren (siehe docs/linux.md) und das Handy danach neu einstecken. Es wurde keine Android-Auto-Sitzung gestartet.";
}
}
