#pragma once
#include "logging/Logger.h"
#include <string>

namespace headunit {
// Exit codes of `HeadUnit.exe --repair-driver`; values are part of the process contract
// between the elevated helper and the window that starts it.
enum class RepairOutcome {
    Fixed = 0,             // WinUSB is now bound to the phone's MI_00 function
    AlreadyOk = 1,         // nothing to do
    NoPhone = 2,           // no 04E8 device present
    InAccessoryMode = 3,   // phone is in AOA mode; its accessory driver needs no repair
    UnsupportedMode = 4,   // phone is present, but not in the file-transfer layout 04E8:6860
    InstallFailed = 5,
    Timeout = 6,           // driver switched, but WinUSB did not appear on MI_00
    NotElevated = 7,
    Failed = 8,
    Cancelled = 9,         // user declined the UAC prompt (parent side only)
};
struct RepairResult {
    RepairOutcome outcome{RepairOutcome::Failed};
    std::string message;
    bool IsUsable() const {
        return outcome == RepairOutcome::Fixed || outcome == RepairOutcome::AlreadyOk || outcome == RepairOutcome::InAccessoryMode;
    }
};
// Does the work in this process; requires administrator rights when a change is needed.
// Rebinds the connected Samsung 04E8:6860 to Microsoft's composite driver, which lets
// Windows pick the already installed WinUSB package for its MI_00 function.
RepairResult RepairPhoneDriverNow(Logger& logger);
// Starts this executable elevated (UAC prompt) with --repair-driver and waits for it.
RepairResult RepairPhoneDriverElevated(Logger& logger);

// Same as unplugging and replugging the phone, then repairing the driver, in ONE elevated
// process (one UAC prompt). A phone stuck in accessory mode without a working Android Auto
// is restarted by disabling and re-enabling its device node; Android leaves accessory mode
// when the bus resets, so the phone comes back as 04E8:6860 (with Samsung's driver, which
// is repaired right away). Used when the phone does not answer.
RepairResult RecoverPhoneNow(Logger& logger);
// Starts this executable elevated with --recover-phone and waits for it.
RepairResult RecoverPhoneElevated(Logger& logger);
}
