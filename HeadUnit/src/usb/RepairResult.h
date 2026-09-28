#pragma once
#include <string>

namespace headunit {
// The outcome of a driver repair or phone restart. The values are also the exit codes of `HeadUnit --repair-driver`
// and `--recover-phone`, the process contract between the elevated helper and the window that starts it.
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

// A repair outcome with the message for the window.
struct RepairResult {
    RepairOutcome outcome{RepairOutcome::Failed};
    std::string message;

    bool IsUsable() const;
};
}
