#pragma once
#include "usb/IUsbBackend.h"
#include "logging/Logger.h"

namespace headunit {
class WindowsUsbBackend final : public IUsbBackend {
public:
    explicit WindowsUsbBackend(Logger& logger) : m_logger(logger) {}
    UsbScanResult EnumerateDevices() override;
private:
    Logger& m_logger;
};
}
