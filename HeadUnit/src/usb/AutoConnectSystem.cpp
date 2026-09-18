#include "usb/AutoConnectSystem.h"
#include <thread>

namespace headunit {
AutoConnectResult ConnectPhoneAutomatically(IUsbBackend& backend, Logger& logger, std::atomic_bool& isStopRequested,
    ProjectionCallbacks callbacks)
{
    AutoConnectDeps deps;
    deps.scan = [&] { return backend.EnumerateDevices(); };
    deps.connect = [&](const UsbDevice& phone) { return ConnectAndroidAuto(phone, logger, isStopRequested, callbacks); };
    deps.repair = [&] { return RepairPhoneDriverElevated(logger); };
    deps.recover = [&] { return RecoverPhoneElevated(logger); };
    // Sleeps in small slices so the Stop button takes effect within 100 ms.
    deps.wait = [&](std::chrono::milliseconds time) {
        for (auto left = time; left.count() > 0 && !isStopRequested; left -= std::chrono::milliseconds(100))
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    };
    return RunAutoConnect(deps, logger, isStopRequested, callbacks.onStatus);
}
}
