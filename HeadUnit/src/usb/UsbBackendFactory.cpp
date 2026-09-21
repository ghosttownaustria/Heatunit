#include "usb/UsbBackendFactory.h"
#include "platform/Environment.h"
#include "usb/LibusbUsbBackend.h"
#ifdef _WIN32
#include "usb/WindowsUsbBackend.h"
#endif

namespace headunit {
std::unique_ptr<IUsbBackend> CreateUsbBackend(Logger& logger)
{
    [[maybe_unused]] const auto choice = GetEnv("HEADUNIT_USB_BACKEND");
#ifdef _WIN32
    if (!choice || *choice != "libusb") return std::make_unique<WindowsUsbBackend>(logger);
    logger.Write("INFO", "USB", "HEADUNIT_USB_BACKEND=libusb: discovery through libusb instead of the Windows hub IOCTLs");
#endif
    return std::make_unique<LibusbUsbBackend>(logger);
}
}
