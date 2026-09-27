#pragma once
#include "logging/Logger.h"
#include <string>

namespace headunit {
// What a phone needs from the operating system before libusb can talk to it, and what the app can do
// about it on its own. Windows: the phone must be bound to WinUSB, which the app repairs (DriverRepair.cpp,
// needs administrator rights). Linux: the user needs access rights to the USB device node, which only an
// administrator can grant once by installing the udev rule (scripts/install-udev-rules.sh); the app can
// tell when it is missing but cannot fix it (DriverRepairLinux.cpp).

// Exit codes of `HeadUnit --repair-driver`; values are part of the process contract
// between the elevated helper and the window that starts it.
enum class RepairOutcome {
    Fixed = 0,             // Windows: WinUSB is now bound to the phone's MI_00 function; both: the phone was restarted
    AlreadyOk = 1,         // nothing to do
    NoPhone = 2,           // no phone present
    InAccessoryMode = 3,   // phone is in AOA mode; its accessory driver needs no repair
    UnsupportedMode = 4,   // Windows: phone is present, but not in the file-transfer layout 04E8:6860
    InstallFailed = 5,
    Timeout = 6,           // driver switched, but WinUSB did not appear on MI_00 / the phone did not come back
    NotElevated = 7,
    Failed = 8,
    Cancelled = 9,         // user declined the UAC prompt (parent side only)
    AccessDenied = 10,     // Linux: the phone cannot be opened, the udev rule is missing
};
struct RepairResult {
    RepairOutcome outcome{RepairOutcome::Failed};
    std::string message;
    bool IsUsable() const {
        return outcome == RepairOutcome::Fixed || outcome == RepairOutcome::AlreadyOk || outcome == RepairOutcome::InAccessoryMode;
    }
};
// Does the work in this process. Windows: requires administrator rights when a change is needed;
// rebinds the connected Samsung 04E8:6860 to Microsoft's composite driver, which lets
// Windows pick the already installed WinUSB package for its MI_00 function. Linux: checks that the phone
// can be opened and says what to install when it cannot.
RepairResult RepairPhoneDriverNow(Logger& logger);
// Windows: starts this executable elevated (UAC prompt) with --repair-driver and waits for it.
// Linux: nothing needs elevation, this is RepairPhoneDriverNow.
RepairResult RepairPhoneDriverElevated(Logger& logger);

// Same as unplugging and replugging the phone, then repairing the driver, in ONE elevated
// process (one UAC prompt). A phone stuck in accessory mode without a working Android Auto
// is restarted by disabling and re-enabling its device node; Android leaves accessory mode
// when the bus resets, so the phone comes back as 04E8:6860 (with Samsung's driver, which
// is repaired right away). Used when the phone does not answer. Linux: the USB link is
// restarted through sysfs (needs write access to it) or a libusb device reset, best effort.
RepairResult RecoverPhoneNow(Logger& logger);
// Windows: starts this executable elevated with --recover-phone and waits for it. Linux: RecoverPhoneNow.
RepairResult RecoverPhoneElevated(Logger& logger);
}
