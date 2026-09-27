#pragma once
#include "logging/Logger.h"
#include "usb/IUsbBackend.h"
#include <string>

struct libusb_device;

namespace headunit {
// The `location` libusb devices get: "usb:<bus>-<port>.<port>...", the way Linux names USB devices in
// sysfs (usb:1-2.3 is /sys/bus/usb/devices/1-2.3). It is stable while the phone stays on its port.
std::string LibusbLocation(libusb_device* device);

// Discovery through libusb, for every platform without a native backend (Linux). Descriptors come from
// what libusb already caches, so no device has to be opened for them. The strings (manufacturer, product,
// serial number) are read from Linux sysfs where possible, which needs no access rights either; only
// otherwise the device is opened for them. A phone that cannot be opened (missing udev rule) is therefore
// still found, with its serial number, which lets the caller explain what is missing.
class LibusbUsbBackend final : public IUsbBackend {
public:
    // A quiet backend writes neither the per-device lines nor the summary: for code that polls.
    explicit LibusbUsbBackend(Logger& logger, bool isQuiet = false) : m_logger(logger), m_isQuiet(isQuiet) {}
    UsbScanResult EnumerateDevices() override;
private:
    Logger& m_logger;
    bool m_isQuiet;
};
}
