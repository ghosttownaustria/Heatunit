#include "usb/AutoConnectSystem.h"
#include "usb/AndroidUsbProbe.h"
#include "usb/DriverRepair.h"

namespace headunit {
// RunAutoConnect wired to the real USB backend, the libusb session and this platform's driver repair (elevated on
// Windows). Runs on a worker; `callbacks.onStatus` receives both the flow's steps and the session's progress.
AutoConnectResult ConnectPhoneAutomatically(IUsbBackend& backend, Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
{
    AutoConnectDeps deps;
    deps.scan = [&] { return backend.EnumerateDevices(); };
    deps.connect = [&](const UsbDevice& phone) { return ConnectAndroidAuto(phone, logger, isStopRequested, callbacks); };
    deps.repair = [&] { return RepairPhoneDriverElevated(logger); };
    deps.recover = [&] { return RecoverPhoneElevated(logger); };
    deps.needsAdminPrompt = NeedsAdminPromptForRepair();
    deps.wait = [&](std::chrono::milliseconds duration) { SleepUnlessStopped(duration, isStopRequested); };
    return RunAutoConnect(deps, logger, isStopRequested, callbacks.onStatus);
}
}
