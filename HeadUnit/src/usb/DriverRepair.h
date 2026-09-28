#pragma once
#include "logging/Logger.h"
#include "usb/RepairResult.h"
#include <string>

// What a phone needs from the operating system before libusb can talk to it, and what the app can do about it on its
// own. Windows (WindowsDriverRepair.cpp): the phone must be bound to WinUSB, which the app repairs with administrator
// rights. Linux (LinuxDriverRepair.cpp): the user needs access rights to the USB device node, which only an
// administrator can grant once by installing the udev rule (scripts/install-udev-rules.sh); the app can tell when it is
// missing but cannot fix it. The build compiles one of the two files.
namespace headunit {
// What a failed libusb_open of the phone in its normal mode means.
struct UsbOpenAdvice {
    bool isDriverIssue{};   // the system gave the phone a driver or permissions libusb cannot use, not a plain failure
    bool canBeRepaired{};   // the app can fix that itself (RepairPhoneDriverElevated)
    std::string advice;     // what to do about it
};

RepairResult RepairPhoneDriverNow(Logger& logger);
RepairResult RepairPhoneDriverElevated(Logger& logger);
RepairResult RecoverPhoneNow(Logger& logger);
RepairResult RecoverPhoneElevated(Logger& logger);
bool NeedsAdminPromptForRepair();
UsbOpenAdvice AdviseOnPhoneOpenFailure(int libusbError);
std::string AccessoryOpenAdvice();
}
