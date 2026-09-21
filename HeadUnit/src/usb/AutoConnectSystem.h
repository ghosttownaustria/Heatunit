#pragma once
#include "androidauto/AutoConnect.h"
#include "usb/IUsbBackend.h"

namespace headunit {
// RunAutoConnect wired to the real USB backend, libusb session and this platform's driver repair
// (elevated on Windows).
// Runs on a worker; onStatus receives both the flow's steps and the session's progress.
AutoConnectResult ConnectPhoneAutomatically(IUsbBackend& backend, Logger& logger, std::atomic_bool& isStopRequested,
    ProjectionCallbacks callbacks);
}
