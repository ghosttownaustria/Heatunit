// Windows implementation of DriverRepair.h: binds WinUSB to the phone and restarts its USB connection.
#include "usb/DriverRepair.h"
#include "platform/WindowsSupport.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <shellapi.h>
#include <libusb.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "shell32.lib")

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr wchar_t kParentPrefix[] = L"USB\\VID_04E8&PID_6860\\";
constexpr wchar_t kFunctionPrefix[] = L"USB\\VID_04E8&PID_6860&MI_00\\";
constexpr wchar_t kAccessoryPrefix[] = L"USB\\VID_18D1&PID_2D0";
constexpr wchar_t kSamsungPrefix[] = L"USB\\VID_04E8&";
constexpr int kMaxRestartAttempts = 3;

// A present USB device node and the service that currently drives it.
struct UsbNode {
    std::wstring instance, service;
};

// Where the phone is, judged by its device nodes.
enum class PhoneMode { Gone, Normal, Accessory };

// Whether a driver-selection function accepts a driver.
using DriverFilter = bool (*)(const SP_DRVINFO_DATA_W&, const SP_DRVINFO_DETAIL_DATA_W&);

// Whether `text` starts with `prefix`, ignoring case.
bool StartsWith(const std::wstring& text, const wchar_t* prefix)
{
    return _wcsnicmp(text.c_str(), prefix, wcslen(prefix)) == 0;
}

// Whether the node is driven by WinUSB.
bool IsWinUsb(const UsbNode& node)
{
    return _wcsicmp(node.service.c_str(), L"WinUSB") == 0;
}

// Whether this process runs with administrator rights.
bool IsElevated()
{
    HANDLE rawToken{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &rawToken)) return false;
    const WindowsHandle token(rawToken);
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
}

// The present USB nodes with the service that currently drives them.
std::vector<UsbNode> Snapshot()
{
    std::vector<UsbNode> nodes;
    const DeviceInfoSet list(SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES));
    if (list.get() == INVALID_HANDLE_VALUE) return nodes;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInfo(list.get(), index, &data)) break;
        wchar_t instance[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(list.get(), &data, instance, static_cast<DWORD>(std::size(instance)), nullptr)) continue;
        wchar_t service[128]{};
        SetupDiGetDeviceRegistryPropertyW(list.get(), &data, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(service), sizeof(service) - sizeof(wchar_t), nullptr);
        nodes.push_back({instance, service});
    }
    return nodes;
}

// The first node whose instance id starts with `prefix`.
const UsbNode* Find(const std::vector<UsbNode>& nodes, const wchar_t* prefix)
{
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const UsbNode& node) { return StartsWith(node.instance, prefix); });
    return found == nodes.end() ? nullptr : &*found;
}

// Opens `instance` in a new device information set and builds its driver list of `type`. With the class list, drivers
// that usb.inf marks ExcludeFromSelect are allowed, the way Device Manager's "let me pick" does it. Empty on failure,
// with `error` set.
DeviceInfoSet OpenDriverList(const std::wstring& instance, DWORD type, SP_DEVINFO_DATA& device, std::string& error)
{
    const GUID usbClass = {0x36FC9E60, 0xC465, 0x11CF, {0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    DeviceInfoSet list(SetupDiCreateDeviceInfoList(type == SPDIT_CLASSDRIVER ? &usbClass : nullptr, nullptr));
    if (list.get() == INVALID_HANDLE_VALUE) { error = WindowsErrorText("SetupDiCreateDeviceInfoList"); return {}; }
    device = SP_DEVINFO_DATA{sizeof(device)};
    if (!SetupDiOpenDeviceInfoW(list.get(), instance.c_str(), nullptr, 0, &device)) { error = WindowsErrorText("SetupDiOpenDeviceInfo"); return {}; }
    if (type == SPDIT_CLASSDRIVER) {
        SP_DEVINSTALL_PARAMS_W params{sizeof(params)};
        if (!SetupDiGetDeviceInstallParamsW(list.get(), &device, &params)) { error = WindowsErrorText("SetupDiGetDeviceInstallParams"); return {}; }
        params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
        if (!SetupDiSetDeviceInstallParamsW(list.get(), &device, &params)) { error = WindowsErrorText("SetupDiSetDeviceInstallParams"); return {}; }
    }
    if (!SetupDiBuildDriverInfoList(list.get(), &device, type)) { error = WindowsErrorText("SetupDiBuildDriverInfoList"); return {}; }
    return list;
}

// Installs the first driver of the list that `isWanted` accepts. True when installed, false on an error (in `error`),
// nothing when the list has no such driver.
std::optional<bool> InstallFirstWanted(HDEVINFO list, SP_DEVINFO_DATA& device, DWORD type, DriverFilter isWanted, const char* description,
    Logger& logger, std::string& error)
{
    std::vector<unsigned char> buffer(sizeof(SP_DRVINFO_DETAIL_DATA_W) + 65536);
    for (DWORD index = 0;; ++index) {
        SP_DRVINFO_DATA_W driver{sizeof(driver)};
        if (!SetupDiEnumDriverInfoW(list, &device, type, index, &driver)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) return std::nullopt;
            error = WindowsErrorText("SetupDiEnumDriverInfo");
            return false;
        }
        auto* detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
        detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
        if (!SetupDiGetDriverInfoDetailW(list, &device, &driver, detail, static_cast<DWORD>(buffer.size()), nullptr)) continue;
        if (!isWanted(driver, *detail)) continue;
        logger.Write(LogLevel::Info, "REPAIR", std::string("Installing ") + description + " from " + WideToUtf8(detail->InfFileName) +
            (type == SPDIT_CLASSDRIVER ? " (class list)" : ""));
        if (!SetupDiSetSelectedDriverW(list, &device, &driver)) { error = WindowsErrorText("SetupDiSetSelectedDriver"); return false; }
        BOOL needsReboot = FALSE;
        if (!DiInstallDevice(nullptr, list, &device, &driver, 0, &needsReboot)) { error = WindowsErrorText("DiInstallDevice"); return false; }
        return true;
    }
}

// Forces the driver that `isWanted` accepts onto one device instance. Windows only chooses by rank on its own, and
// Samsung's package outranks the inbox one. The compatible list is searched first. With `isClassListAllowed` the whole
// USB class list is searched afterwards; that is the only place the generic composite driver is offered for a phone that
// shows a single interface (and the class list must be built before any compatible list).
bool InstallDriver(const std::wstring& instance, DriverFilter isWanted, const char* description, bool isClassListAllowed, Logger& logger,
    std::string& error)
{
    for (const DWORD type : {static_cast<DWORD>(SPDIT_COMPATDRIVER), static_cast<DWORD>(SPDIT_CLASSDRIVER)}) {
        if (type == SPDIT_CLASSDRIVER && !isClassListAllowed) break;
        SP_DEVINFO_DATA device{};
        const DeviceInfoSet list = OpenDriverList(instance, type, device, error);
        if (!list) return false;
        if (const auto isInstalled = InstallFirstWanted(list.get(), device, type, isWanted, description, logger, error)) return *isInstalled;
    }
    error = std::string("No matching driver found for ") + description;
    return false;
}

// Microsoft's generic composite driver (usbccgp): the usb.inf entry for hardware ID USB\COMPOSITE. usb.inf holds many
// vendor-specific entries in the same section and hubs in others; only the generic one may be forced onto the phone.
bool IsInboxComposite(const SP_DRVINFO_DATA_W&, const SP_DRVINFO_DETAIL_DATA_W& detail)
{
    const std::filesystem::path inf(detail.InfFileName);
    return _wcsicmp(inf.filename().c_str(), L"usb.inf") == 0 && _wcsicmp(detail.SectionName, L"Composite.Dev") == 0 &&
        _wcsicmp(detail.HardwareID, L"USB\\COMPOSITE") == 0;
}

// The WinUSB package created earlier with libwdi (oem*.inf, original name mtp_(interface_0).inf).
bool IsWinUsbPackage(const SP_DRVINFO_DATA_W& driver, const SP_DRVINFO_DETAIL_DATA_W&)
{
    return _wcsicmp(driver.ProviderName, L"libwdi") == 0;
}

// DICS_DISABLE / DICS_ENABLE for one device node (what Device Manager's disable/enable does).
bool ChangeDeviceState(const std::wstring& instance, DWORD state, std::string& error)
{
    const DeviceInfoSet list(SetupDiCreateDeviceInfoList(nullptr, nullptr));
    if (list.get() == INVALID_HANDLE_VALUE) { error = WindowsErrorText("SetupDiCreateDeviceInfoList"); return false; }
    SP_DEVINFO_DATA device{sizeof(device)};
    if (!SetupDiOpenDeviceInfoW(list.get(), instance.c_str(), nullptr, 0, &device)) { error = WindowsErrorText("SetupDiOpenDeviceInfo"); return false; }
    SP_PROPCHANGE_PARAMS params{};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = state;
    params.Scope = DICS_FLAG_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(list.get(), &device, &params.ClassInstallHeader, sizeof(params)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, list.get(), &device)) {
        error = WindowsErrorText(state == DICS_DISABLE ? "Disabling the device" : "Enabling the device");
        return false;
    }
    return true;
}

// Waits for the MI_00 function to exist and report WinUSB.
bool WaitForWinUsb(std::chrono::seconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        const auto nodes = Snapshot();
        const auto* function = Find(nodes, kFunctionPrefix);
        if (function && IsWinUsb(*function)) return true;
        std::this_thread::sleep_for(500ms);
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

// Where the phone is right now.
PhoneMode CurrentMode()
{
    const auto nodes = Snapshot();
    if (Find(nodes, kParentPrefix)) return PhoneMode::Normal;
    if (Find(nodes, kAccessoryPrefix)) return PhoneMode::Accessory;
    return PhoneMode::Gone;
}

// Disables the accessory device node for `offTime` and enables it again, which makes Windows re-enumerate the phone on
// the bus. Returns the mode the phone came back in. A phone that never drops off did not see the link go away, so that
// attempt failed and is not waited out.
PhoneMode RestartAccessoryNode(const std::wstring& instance, std::chrono::milliseconds offTime, Logger& logger, std::string& error)
{
    if (!ChangeDeviceState(instance, DICS_DISABLE, error)) return PhoneMode::Gone;
    std::this_thread::sleep_for(offTime);
    if (!ChangeDeviceState(instance, DICS_ENABLE, error)) return PhoneMode::Gone;
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + 25s;
    bool hasLeft = false;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(500ms);
        const auto mode = CurrentMode();
        if (mode == PhoneMode::Gone) hasLeft = true;
        else if (mode == PhoneMode::Normal || hasLeft) return mode;
        else if (std::chrono::steady_clock::now() - begin > 9s) break;
    }
    logger.Write(LogLevel::Warning, "REPAIR", hasLeft ? "Phone did not come back within 25 seconds" : "Phone stayed in accessory mode");
    return CurrentMode();
}

// Restarts the phone that is stuck in accessory mode until it is back in its normal mode. The phone only leaves
// accessory mode when it sees the USB link go away for long enough; a short off time sometimes leaves it in accessory
// mode, so the time grows per attempt. Returns the failure, or nothing once the phone is back.
std::optional<RepairResult> RestartUntilNormalMode(const std::wstring& instance, Logger& logger)
{
    PhoneMode mode = PhoneMode::Accessory;
    for (int attempt = 1; attempt <= kMaxRestartAttempts && mode != PhoneMode::Normal; ++attempt) {
        const auto offTime = std::chrono::milliseconds(3000 + 2500 * (attempt - 1));
        logger.Write(LogLevel::Info, "REPAIR", "Restarting accessory device " + WideToUtf8(instance) + " (attempt " + std::to_string(attempt) +
            ", off for " + std::to_string(offTime.count()) + " ms)");
        std::string error;
        mode = RestartAccessoryNode(instance, offTime, logger, error);
        if (!error.empty()) {
            logger.Write(LogLevel::Error, "REPAIR", error);
            return RepairResult{RepairOutcome::InstallFailed, error};
        }
        if (mode != PhoneMode::Accessory) continue;
        // Still (or again) in accessory mode: the instance may have been recreated.
        const auto nodes = Snapshot();
        const auto* again = Find(nodes, kAccessoryPrefix);
        if (!again) break;
        if (again->instance != instance) logger.Write(LogLevel::Warning, "REPAIR", "Accessory instance changed to " + WideToUtf8(again->instance));
    }
    if (mode != PhoneMode::Normal) {
        logger.Write(LogLevel::Error, "REPAIR", "Phone did not come back as 04E8:6860 after the restart");
        return RepairResult{RepairOutcome::Timeout,
            "Das Handy hat sich nach dem USB-Neustart nicht als 04E8:6860 zurueckgemeldet. Bitte das Kabel einmal abziehen und wieder anstecken."};
    }
    logger.Write(LogLevel::Info, "REPAIR", "Phone is back in normal mode");
    std::this_thread::sleep_for(2s);   // let Windows finish its own driver installation first
    return std::nullopt;
}

// The window's text for the exit code of the elevated helper; unknown codes count as a failed repair.
RepairResult ElevatedHelperResult(DWORD exitCode)
{
    RepairResult result{static_cast<RepairOutcome>(exitCode), {}};
    switch (result.outcome) {
    case RepairOutcome::Fixed: result.message = "Treiber repariert (WinUSB gebunden)."; break;
    case RepairOutcome::AlreadyOk: result.message = "WinUSB war bereits gebunden."; break;
    case RepairOutcome::InAccessoryMode: result.message = "Handy ist im Accessory-Modus."; break;
    case RepairOutcome::NoPhone: result.message = "Kein Samsung-Handy per USB gefunden. Kabel und Entsperrung pruefen."; break;
    case RepairOutcome::UnsupportedMode: result.message = "Das Handy meldet sich nicht als 04E8:6860. Am Handy den USB-Modus auf Dateiuebertragung stellen."; break;
    case RepairOutcome::Timeout: result.message = "Das Handy hat sich nach dem USB-Neustart nicht zurueckgemeldet. Kabel einmal abziehen und wieder anstecken."; break;
    default:
        result.outcome = RepairOutcome::InstallFailed;
        result.message = "Die Treiberreparatur ist fehlgeschlagen; Details in headunit-repair.log.";
        break;
    }
    return result;
}

// Starts this executable elevated (UAC prompt) with `argument` and waits up to `timeoutMs` for its result.
RepairResult RunElevated(Logger& logger, const wchar_t* argument, DWORD timeoutMs)
{
    wchar_t executable[MAX_PATH]{};
    GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    const auto directory = std::filesystem::current_path().wstring();
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = executable;
    info.lpParameters = argument;
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    logger.Write(LogLevel::Info, "REPAIR", "Starting elevated helper " + WideToUtf8(argument) + " (UAC prompt)");
    if (!ShellExecuteExW(&info)) {
        if (GetLastError() == ERROR_CANCELLED) return {RepairOutcome::Cancelled, "Die Administrator-Abfrage wurde abgelehnt."};
        return {RepairOutcome::Failed, WindowsErrorText("ShellExecuteEx")};
    }
    const WindowsHandle process(info.hProcess);
    if (WaitForSingleObject(process.get(), timeoutMs) != WAIT_OBJECT_0) return {RepairOutcome::Timeout, "Die Treiberreparatur hat nicht rechtzeitig geantwortet."};
    DWORD exitCode = static_cast<DWORD>(RepairOutcome::Failed);
    GetExitCodeProcess(process.get(), &exitCode);
    const auto result = ElevatedHelperResult(exitCode);
    logger.Write(result.IsUsable() ? LogLevel::Info : LogLevel::Error, "REPAIR", result.message);
    return result;
}
}

// Rebinds the connected Samsung 04E8:6860 to Microsoft's composite driver, which lets Windows pick the already installed
// WinUSB package for its MI_00 function; when MI_00 keeps another driver, WinUSB is bound to it directly. Needs
// administrator rights when a change is needed.
RepairResult RepairPhoneDriverNow(Logger& logger)
{
    auto nodes = Snapshot();
    const auto* parent = Find(nodes, kParentPrefix);
    const auto* function = Find(nodes, kFunctionPrefix);
    if (!parent) {
        if (Find(nodes, kAccessoryPrefix)) return {RepairOutcome::InAccessoryMode, "Handy ist im Accessory-Modus; dafuer ist kein Treiber-Eingriff noetig."};
        if (Find(nodes, kSamsungPrefix))
            return {RepairOutcome::UnsupportedMode, "Das Handy meldet sich nicht als 04E8:6860 (Dateiuebertragung). Bitte am Handy den USB-Modus auf Dateiuebertragung stellen."};
        return {RepairOutcome::NoPhone, "Kein Samsung-Handy per USB gefunden. Kabel und Entsperrung pruefen."};
    }
    logger.Write(LogLevel::Info, "REPAIR", "Phone parent " + WideToUtf8(parent->instance) + " driven by " + WideToUtf8(parent->service) +
        (function ? "; MI_00 driven by " + WideToUtf8(function->service) : "; no MI_00 function"));
    if (function && IsWinUsb(*function)) return {RepairOutcome::AlreadyOk, "WinUSB ist bereits gebunden."};
    if (!IsElevated()) return {RepairOutcome::NotElevated, "Administratorrechte sind noetig."};

    std::string error;
    if (_wcsicmp(parent->service.c_str(), L"usbccgp") != 0) {
        if (!InstallDriver(parent->instance, IsInboxComposite, "Microsoft USB composite driver", true, logger, error)) {
            logger.Write(LogLevel::Error, "REPAIR", error);
            return {RepairOutcome::InstallFailed, error};
        }
        if (WaitForWinUsb(20s)) return {RepairOutcome::Fixed, "WinUSB wurde gebunden."};
        nodes = Snapshot();
        function = Find(nodes, kFunctionPrefix);
    }
    // The composite parent is right but MI_00 kept another driver: bind the WinUSB package to it directly.
    if (function && !IsWinUsb(*function)) {
        if (!InstallDriver(function->instance, IsWinUsbPackage, "WinUSB package (libwdi) for MI_00", false, logger, error)) {
            logger.Write(LogLevel::Error, "REPAIR", error);
            return {RepairOutcome::InstallFailed, error + ". Das WinUSB-Paket fehlt eventuell; siehe docs/windows_connection.md."};
        }
        if (WaitForWinUsb(15s)) return {RepairOutcome::Fixed, "WinUSB wurde gebunden."};
    }
    logger.Write(LogLevel::Error, "REPAIR", "MI_00 did not report WinUSB in time");
    return {RepairOutcome::Timeout, "Der Treiber wurde umgestellt, aber MI_00 meldet noch kein WinUSB. Kabel einmal abziehen und wieder anstecken."};
}

// The same as unplugging and replugging the phone, then repairing the driver, in one elevated process (one UAC
// prompt). A phone stuck in accessory mode without a working Android Auto is restarted by disabling and re-enabling its
// device node; Android leaves accessory mode when the bus resets, so the phone comes back as 04E8:6860 (with Samsung's
// driver, which is repaired right away).
RepairResult RecoverPhoneNow(Logger& logger)
{
    if (!IsElevated()) return {RepairOutcome::NotElevated, "Administratorrechte sind noetig."};
    const auto nodes = Snapshot();
    bool wasRestarted = false;
    if (const auto* accessory = Find(nodes, kAccessoryPrefix)) {
        if (const auto failure = RestartUntilNormalMode(accessory->instance, logger)) return *failure;
        wasRestarted = true;
    }
    auto result = RepairPhoneDriverNow(logger);
    // Nothing left to repair after a restart is still a success: the phone was restarted.
    if (wasRestarted && result.IsUsable()) {
        result.outcome = RepairOutcome::Fixed;
        result.message = "Handy neu gestartet, Treiber in Ordnung.";
    }
    return result;
}

// Starts this executable elevated with --repair-driver and waits for it.
RepairResult RepairPhoneDriverElevated(Logger& logger)
{
    return RunElevated(logger, L"--repair-driver", 120000);
}

// Starts this executable elevated with --recover-phone and waits for it.
RepairResult RecoverPhoneElevated(Logger& logger)
{
    auto result = RunElevated(logger, L"--recover-phone", 240000);
    // Fixed means the phone was restarted (the helper turns "driver already fine" into Fixed then).
    if (result.outcome == RepairOutcome::Fixed) result.message = "Handy neu gestartet, Treiber in Ordnung.";
    return result;
}

// Repair and restart ask for administrator rights through a UAC prompt, which the user has to confirm.
bool NeedsAdminPromptForRepair()
{
    return true;
}

// A phone that Windows lists but libusb cannot open is bound to the Samsung/MTP driver instead of WinUSB, which the app
// can repair.
UsbOpenAdvice AdviseOnPhoneOpenFailure(int libusbError)
{
    const bool isDriverIssue = libusbError == LIBUSB_ERROR_NOT_FOUND || libusbError == LIBUSB_ERROR_NOT_SUPPORTED || libusbError == LIBUSB_ERROR_ACCESS;
    if (!isDriverIssue) return {false, false, "Device open failed; rescan and inspect the USB connection."};
    return {true, true,
        "Windows exposes the device for discovery, but libusb cannot access it: the phone is bound to the Samsung/MTP driver instead of WinUSB "
        "(Windows re-selects the driver whenever the phone re-appears in file-transfer mode). See docs/windows_connection.md. No Android Auto handshake was sent."};
}

// The phone in accessory mode shows up with a new VID/PID, which needs its own WinUSB binding.
std::string AccessoryOpenAdvice()
{
    return "Phone is in accessory mode, but its NEW VID/PID needs a usable WinUSB binding. No AA session started.";
}
}
