#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/AutoConnect.h"
#include "usb/IUsbBackend.h"
#include <atomic>

namespace headunit {
AutoConnectResult ConnectPhoneAutomatically(IUsbBackend& backend, Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
