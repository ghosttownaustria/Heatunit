#pragma once
#include "logging/Logger.h"
#include "usb/IUsbBackend.h"
#include <string>

struct libusb_device;

namespace headunit {
// Discovery through libusb, for every platform without a native backend (Linux). Descriptors come from what libusb
// already caches, so no device has to be opened for them. The strings (manufacturer, product, serial number) are read
// from Linux sysfs where possible, which needs no access rights either; only otherwise the device is opened for them.
// A phone that cannot be opened (missing udev rule) is therefore still found, with its serial number, which lets the
// caller explain what is missing.
class LibusbUsbBackend final : public IUsbBackend {
public:
    explicit LibusbUsbBackend(Logger& logger, bool isQuiet = false);

    UsbScanResult EnumerateDevices() override;

private:
    Logger& m_logger;
    bool m_isQuiet;
};

std::string LibusbLocation(libusb_device* device);
}
