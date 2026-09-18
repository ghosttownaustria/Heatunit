#include "usb/DriverRepair.h"
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <newdev.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "newdev.lib")
#pragma comment(lib, "shell32.lib")

namespace headunit {
namespace {
constexpr wchar_t kParentPrefix[] = L"USB\\VID_04E8&PID_6860\\";
constexpr wchar_t kFunctionPrefix[] = L"USB\\VID_04E8&PID_6860&MI_00\\";
constexpr wchar_t kAccessoryPrefix[] = L"USB\\VID_18D1&PID_2D0";
constexpr wchar_t kSamsungPrefix[] = L"USB\\VID_04E8&";

struct UsbNode { std::wstring instance, service; };
struct DevInfoList {
    HDEVINFO handle{INVALID_HANDLE_VALUE};
    explicit DevInfoList(HDEVINFO value) : handle(value) {}
    DevInfoList(const DevInfoList&) = delete;
    DevInfoList& operator=(const DevInfoList&) = delete;
    ~DevInfoList() { if (handle != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(handle); }
};
std::string Narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}
bool StartsWith(const std::wstring& text, const wchar_t* prefix) {
    return _wcsnicmp(text.c_str(), prefix, wcslen(prefix)) == 0;
}
std::string LastError(const char* what) {
    return std::string(what) + " failed (Win32 " + std::to_string(GetLastError()) + ")";
}
bool IsElevated() {
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD size{};
    const bool isElevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) && elevation.TokenIsElevated;
    CloseHandle(token);
    return isElevated;
}
// Present USB nodes with the service that currently drives them.
std::vector<UsbNode> Snapshot() {
    std::vector<UsbNode> nodes;
    DevInfoList list(SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES));
    if (list.handle == INVALID_HANDLE_VALUE) return nodes;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA data{sizeof(data)};
        if (!SetupDiEnumDeviceInfo(list.handle, index, &data)) break;
        wchar_t instance[MAX_DEVICE_ID_LEN]{};
        if (!SetupDiGetDeviceInstanceIdW(list.handle, &data, instance, static_cast<DWORD>(std::size(instance)), nullptr)) continue;
        wchar_t service[128]{};
        SetupDiGetDeviceRegistryPropertyW(list.handle, &data, SPDRP_SERVICE, nullptr, reinterpret_cast<PBYTE>(service), sizeof(service) - sizeof(wchar_t), nullptr);
        nodes.push_back({instance, service});
    }
    return nodes;
}
const UsbNode* Find(const std::vector<UsbNode>& nodes, const wchar_t* prefix) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&](const UsbNode& node) { return StartsWith(node.instance, prefix); });
    return found == nodes.end() ? nullptr : &*found;
}
// Forces the driver whose INF matches `isWanted` onto one device instance. Windows only chooses
// by rank on its own, and Samsung's package outranks the inbox one. The compatible list is
// searched first. With `allowClassList` the whole USB class list is searched afterwards, the way
// Device Manager's "let me pick" does it; that is the only place the generic composite driver is
// offered for a phone that shows a single interface (usb.inf marks it ExcludeFromSelect, hence
// DI_FLAGSEX_ALLOWEXCLUDEDDRVS, and the class list must be built before any compatible list).
bool InstallDriver(const std::wstring& instance, bool (*isWanted)(const SP_DRVINFO_DATA_W&, const SP_DRVINFO_DETAIL_DATA_W&),
    const char* description, bool allowClassList, Logger& logger, std::string& error) {
    const GUID usbClass = {0x36FC9E60, 0xC465, 0x11CF, {0x80, 0x56, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
    std::vector<unsigned char> buffer(sizeof(SP_DRVINFO_DETAIL_DATA_W) + 65536);
    for (const DWORD type : {static_cast<DWORD>(SPDIT_COMPATDRIVER), static_cast<DWORD>(SPDIT_CLASSDRIVER)}) {
        if (type == SPDIT_CLASSDRIVER && !allowClassList) break;
        DevInfoList list(SetupDiCreateDeviceInfoList(type == SPDIT_CLASSDRIVER ? &usbClass : nullptr, nullptr));
        if (list.handle == INVALID_HANDLE_VALUE) { error = LastError("SetupDiCreateDeviceInfoList"); return false; }
        SP_DEVINFO_DATA device{sizeof(device)};
        if (!SetupDiOpenDeviceInfoW(list.handle, instance.c_str(), nullptr, 0, &device)) { error = LastError("SetupDiOpenDeviceInfo"); return false; }
        if (type == SPDIT_CLASSDRIVER) {
            SP_DEVINSTALL_PARAMS_W params{sizeof(params)};
            if (!SetupDiGetDeviceInstallParamsW(list.handle, &device, &params)) { error = LastError("SetupDiGetDeviceInstallParams"); return false; }
            params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
            if (!SetupDiSetDeviceInstallParamsW(list.handle, &device, &params)) { error = LastError("SetupDiSetDeviceInstallParams"); return false; }
        }
        if (!SetupDiBuildDriverInfoList(list.handle, &device, type)) { error = LastError("SetupDiBuildDriverInfoList"); return false; }
        for (DWORD index = 0;; ++index) {
            SP_DRVINFO_DATA_W driver{sizeof(driver)};
            if (!SetupDiEnumDriverInfoW(list.handle, &device, type, index, &driver)) {
                if (GetLastError() != ERROR_NO_MORE_ITEMS) { error = LastError("SetupDiEnumDriverInfo"); return false; }
                break;
            }
            auto* detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DRVINFO_DETAIL_DATA_W);
            if (!SetupDiGetDriverInfoDetailW(list.handle, &device, &driver, detail, static_cast<DWORD>(buffer.size()), nullptr)) continue;
            if (!isWanted(driver, *detail)) continue;
            logger.Write("INFO", "REPAIR", std::string("Installing ") + description + " from " + Narrow(detail->InfFileName) +
                (type == SPDIT_CLASSDRIVER ? " (class list)" : ""));
            if (!SetupDiSetSelectedDriverW(list.handle, &device, &driver)) { error = LastError("SetupDiSetSelectedDriver"); return false; }
            BOOL needsReboot = FALSE;
            if (!DiInstallDevice(nullptr, list.handle, &device, &driver, 0, &needsReboot)) { error = LastError("DiInstallDevice"); return false; }
            return true;
        }
    }
    error = std::string("No matching driver found for ") + description;
    return false;
}
// Microsoft's generic composite driver (usbccgp): the usb.inf entry for hardware ID USB\COMPOSITE.
// usb.inf holds many vendor-specific entries in the same section and hubs in others; only the
// generic one may be forced onto the phone.
bool IsInboxComposite(const SP_DRVINFO_DATA_W&, const SP_DRVINFO_DETAIL_DATA_W& detail) {
    const std::filesystem::path inf(detail.InfFileName);
    return _wcsicmp(inf.filename().c_str(), L"usb.inf") == 0 && _wcsicmp(detail.SectionName, L"Composite.Dev") == 0 &&
        _wcsicmp(detail.HardwareID, L"USB\\COMPOSITE") == 0;
}
// The WinUSB package created earlier with libwdi (oem*.inf, original name mtp_(interface_0).inf).
bool IsWinUsbPackage(const SP_DRVINFO_DATA_W& driver, const SP_DRVINFO_DETAIL_DATA_W&) {
    return _wcsicmp(driver.ProviderName, L"libwdi") == 0;
}
// DICS_DISABLE / DICS_ENABLE for one device node (what Device Manager's disable/enable does).
bool ChangeDeviceState(const std::wstring& instance, DWORD state, std::string& error) {
    DevInfoList list(SetupDiCreateDeviceInfoList(nullptr, nullptr));
    if (list.handle == INVALID_HANDLE_VALUE) { error = LastError("SetupDiCreateDeviceInfoList"); return false; }
    SP_DEVINFO_DATA device{sizeof(device)};
    if (!SetupDiOpenDeviceInfoW(list.handle, instance.c_str(), nullptr, 0, &device)) { error = LastError("SetupDiOpenDeviceInfo"); return false; }
    SP_PROPCHANGE_PARAMS params{};
    params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
    params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
    params.StateChange = state;
    params.Scope = DICS_FLAG_GLOBAL;
    if (!SetupDiSetClassInstallParamsW(list.handle, &device, &params.ClassInstallHeader, sizeof(params)) ||
        !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, list.handle, &device)) {
        error = LastError(state == DICS_DISABLE ? "Disabling the device" : "Enabling the device");
        return false;
    }
    return true;
}
// Waits for the MI_00 function to exist and report WinUSB.
bool WaitForWinUsb(std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        const auto nodes = Snapshot();
        const auto* function = Find(nodes, kFunctionPrefix);
        if (function && _wcsicmp(function->service.c_str(), L"WinUSB") == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}
}

RepairResult RepairPhoneDriverNow(Logger& logger) {
    auto nodes = Snapshot();
    const auto* parent = Find(nodes, kParentPrefix);
    const auto* function = Find(nodes, kFunctionPrefix);
    if (!parent) {
        if (Find(nodes, kAccessoryPrefix)) return {RepairOutcome::InAccessoryMode, "Handy ist im Accessory-Modus; dafuer ist kein Treiber-Eingriff noetig."};
        if (Find(nodes, kSamsungPrefix)) return {RepairOutcome::UnsupportedMode, "Das Handy meldet sich nicht als 04E8:6860 (Dateiuebertragung). Bitte am Handy den USB-Modus auf Dateiuebertragung stellen."};
        return {RepairOutcome::NoPhone, "Kein Samsung-Handy per USB gefunden. Kabel und Entsperrung pruefen."};
    }
    logger.Write("INFO", "REPAIR", "Phone parent " + Narrow(parent->instance) + " driven by " + Narrow(parent->service) +
        (function ? "; MI_00 driven by " + Narrow(function->service) : "; no MI_00 function"));
    if (function && _wcsicmp(function->service.c_str(), L"WinUSB") == 0) return {RepairOutcome::AlreadyOk, "WinUSB ist bereits gebunden."};
    if (!IsElevated()) return {RepairOutcome::NotElevated, "Administratorrechte sind noetig."};

    std::string error;
    if (_wcsicmp(parent->service.c_str(), L"usbccgp") != 0) {
        if (!InstallDriver(parent->instance, IsInboxComposite, "Microsoft USB composite driver", true, logger, error)) {
            logger.Write("ERROR", "REPAIR", error);
            return {RepairOutcome::InstallFailed, error};
        }
        if (WaitForWinUsb(std::chrono::seconds(20))) return {RepairOutcome::Fixed, "WinUSB wurde gebunden."};
        nodes = Snapshot();
        function = Find(nodes, kFunctionPrefix);
    }
    // The composite parent is right but MI_00 kept another driver: bind the WinUSB package to it directly.
    if (function && _wcsicmp(function->service.c_str(), L"WinUSB") != 0) {
        if (!InstallDriver(function->instance, IsWinUsbPackage, "WinUSB package (libwdi) for MI_00", false, logger, error)) {
            logger.Write("ERROR", "REPAIR", error);
            return {RepairOutcome::InstallFailed, error + ". Das WinUSB-Paket fehlt eventuell; siehe docs/windows_connection.md."};
        }
        if (WaitForWinUsb(std::chrono::seconds(15))) return {RepairOutcome::Fixed, "WinUSB wurde gebunden."};
    }
    logger.Write("ERROR", "REPAIR", "MI_00 did not report WinUSB in time");
    return {RepairOutcome::Timeout, "Der Treiber wurde umgestellt, aber MI_00 meldet noch kein WinUSB. Kabel einmal abziehen und wieder anstecken."};
}

namespace {
enum class PhoneMode { Gone, Normal, Accessory };
PhoneMode CurrentMode() {
    const auto nodes = Snapshot();
    if (Find(nodes, kParentPrefix)) return PhoneMode::Normal;
    if (Find(nodes, kAccessoryPrefix)) return PhoneMode::Accessory;
    return PhoneMode::Gone;
}
// Disables the accessory device node for `offTime` and enables it again, which makes Windows
// re-enumerate the phone on the bus. Returns the mode the phone came back in.
PhoneMode RestartAccessoryNode(const std::wstring& instance, std::chrono::milliseconds offTime, Logger& logger, std::string& error) {
    if (!ChangeDeviceState(instance, DICS_DISABLE, error)) return PhoneMode::Gone;
    std::this_thread::sleep_for(offTime);
    if (!ChangeDeviceState(instance, DICS_ENABLE, error)) return PhoneMode::Gone;
    // The phone drops off the bus for a few seconds and returns; wait for what it comes back as. A phone
    // that never drops off did not see the link go away, so the attempt failed: do not wait it out.
    const auto begin = std::chrono::steady_clock::now();
    const auto deadline = begin + std::chrono::seconds(25);
    bool hasLeft = false;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        const auto mode = CurrentMode();
        if (mode == PhoneMode::Gone) hasLeft = true;
        else if (mode == PhoneMode::Normal || hasLeft) return mode;
        else if (std::chrono::steady_clock::now() - begin > std::chrono::seconds(9)) break;
    }
    logger.Write("WARN", "REPAIR", hasLeft ? "Phone did not come back within 25 seconds" : "Phone stayed in accessory mode");
    return CurrentMode();
}
}

RepairResult RecoverPhoneNow(Logger& logger) {
    if (!IsElevated()) return {RepairOutcome::NotElevated, "Administratorrechte sind noetig."};
    auto nodes = Snapshot();
    bool wasRestarted = false;
    if (const auto* accessory = Find(nodes, kAccessoryPrefix)) {
        // The phone only leaves accessory mode when it sees the USB link go away for long enough;
        // a short off time sometimes leaves it in accessory mode, so the time grows per attempt.
        const auto instance = accessory->instance;
        PhoneMode mode = PhoneMode::Accessory;
        for (int attempt = 1; attempt <= 3 && mode != PhoneMode::Normal; ++attempt) {
            const auto offTime = std::chrono::milliseconds(3000 + 2500 * (attempt - 1));
            logger.Write("INFO", "REPAIR", "Restarting accessory device " + Narrow(instance) + " (attempt " + std::to_string(attempt) +
                ", off for " + std::to_string(offTime.count()) + " ms)");
            std::string error;
            mode = RestartAccessoryNode(instance, offTime, logger, error);
            if (!error.empty()) { logger.Write("ERROR", "REPAIR", error); return {RepairOutcome::InstallFailed, error}; }
            if (mode == PhoneMode::Accessory) {
                // Still (or again) in accessory mode: the instance may have been recreated.
                nodes = Snapshot();
                const auto* again = Find(nodes, kAccessoryPrefix);
                if (!again) break;
                if (Narrow(again->instance) != Narrow(instance)) logger.Write("WARN", "REPAIR", "Accessory instance changed to " + Narrow(again->instance));
            }
        }
        if (mode != PhoneMode::Normal) {
            logger.Write("ERROR", "REPAIR", "Phone did not come back as 04E8:6860 after the restart");
            return {RepairOutcome::Timeout, "Das Handy hat sich nach dem USB-Neustart nicht als 04E8:6860 zurueckgemeldet. Bitte das Kabel einmal abziehen und wieder anstecken."};
        }
        logger.Write("INFO", "REPAIR", "Phone is back in normal mode");
        wasRestarted = true;
        std::this_thread::sleep_for(std::chrono::seconds(2));  // let Windows finish its own driver installation first
    }
    auto result = RepairPhoneDriverNow(logger);
    // Nothing left to repair after a restart is still a success: the phone was restarted.
    if (wasRestarted && result.IsUsable()) { result.outcome = RepairOutcome::Fixed; result.message = "Handy neu gestartet, Treiber in Ordnung."; }
    return result;
}

namespace {
RepairResult RunElevated(Logger& logger, const wchar_t* argument, int timeoutMs) {
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, static_cast<DWORD>(std::size(exe)));
    const auto directory = std::filesystem::current_path().wstring();
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = exe;
    info.lpParameters = argument;
    info.lpDirectory = directory.c_str();
    info.nShow = SW_HIDE;
    logger.Write("INFO", "REPAIR", "Starting elevated helper " + Narrow(argument) + " (UAC prompt)");
    if (!ShellExecuteExW(&info)) {
        if (GetLastError() == ERROR_CANCELLED) return {RepairOutcome::Cancelled, "Die Administrator-Abfrage wurde abgelehnt."};
        return {RepairOutcome::Failed, LastError("ShellExecuteEx")};
    }
    const DWORD wait = WaitForSingleObject(info.hProcess, static_cast<DWORD>(timeoutMs));
    DWORD code = static_cast<DWORD>(RepairOutcome::Failed);
    if (wait == WAIT_OBJECT_0) GetExitCodeProcess(info.hProcess, &code);
    CloseHandle(info.hProcess);
    if (wait != WAIT_OBJECT_0) return {RepairOutcome::Timeout, "Die Treiberreparatur hat nicht rechtzeitig geantwortet."};
    RepairResult result{static_cast<RepairOutcome>(code), {}};
    switch (result.outcome) {
    case RepairOutcome::Fixed: result.message = "Treiber repariert (WinUSB gebunden)."; break;
    case RepairOutcome::AlreadyOk: result.message = "WinUSB war bereits gebunden."; break;
    case RepairOutcome::InAccessoryMode: result.message = "Handy ist im Accessory-Modus."; break;
    case RepairOutcome::NoPhone: result.message = "Kein Samsung-Handy per USB gefunden. Kabel und Entsperrung pruefen."; break;
    case RepairOutcome::UnsupportedMode: result.message = "Das Handy meldet sich nicht als 04E8:6860. Am Handy den USB-Modus auf Dateiuebertragung stellen."; break;
    case RepairOutcome::Timeout: result.message = "Das Handy hat sich nach dem USB-Neustart nicht zurueckgemeldet. Kabel einmal abziehen und wieder anstecken."; break;
    default: result.outcome = RepairOutcome::InstallFailed; result.message = "Die Treiberreparatur ist fehlgeschlagen; Details in headunit-repair.log."; break;
    }
    logger.Write(result.IsUsable() ? "INFO" : "ERROR", "REPAIR", result.message);
    return result;
}
}

RepairResult RepairPhoneDriverElevated(Logger& logger) { return RunElevated(logger, L"--repair-driver", 120000); }
RepairResult RecoverPhoneElevated(Logger& logger) {
    auto result = RunElevated(logger, L"--recover-phone", 240000);
    // Fixed means the phone was restarted (the helper turns "driver already fine" into Fixed then).
    if (result.outcome == RepairOutcome::Fixed) result.message = "Handy neu gestartet, Treiber in Ordnung.";
    return result;
}
}
