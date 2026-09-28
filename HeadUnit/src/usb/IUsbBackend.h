#pragma once
#include "usb/UsbTypes.h"

namespace headunit {
// Discovery of the devices on the USB bus, one implementation per platform (see CreateUsbBackend).
class IUsbBackend {
public:
    virtual ~IUsbBackend() = default;

    // Reads the identity, strings and descriptors of every device on the bus; throws when the bus cannot be read at all.
    virtual UsbScanResult EnumerateDevices() = 0;
};
}
