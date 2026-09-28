#pragma once
#include "logging/Logger.h"
#include "usb/IUsbBackend.h"

namespace headunit {
// Discovery on Windows through SetupAPI and the USB hub IOCTLs: read-only descriptor requests to every hub port, which
// also work for a phone that is bound to another vendor's driver (libusb could not open that one).
class WindowsUsbBackend final : public IUsbBackend {
public:
    explicit WindowsUsbBackend(Logger& logger);

    UsbScanResult EnumerateDevices() override;

private:
    Logger& m_logger;
};
}
