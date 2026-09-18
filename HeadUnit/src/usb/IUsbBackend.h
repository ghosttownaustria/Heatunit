#pragma once
#include "usb/UsbTypes.h"

namespace headunit {
// Discovery only. The later transport must expose byte counts, timeouts and cancellation.
class IUsbBackend {
public:
    virtual ~IUsbBackend() = default;
    virtual UsbScanResult EnumerateDevices() = 0;
};
}
