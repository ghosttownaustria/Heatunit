#include "usb/UsbBackendFactory.h"
#include "platform/Environment.h"
#include "usb/LibusbUsbBackend.h"
#ifdef _WIN32
#include "usb/WindowsUsbBackend.h"
#endif

namespace headunit {
// The discovery backend of this platform. Windows reads the hub descriptors through SetupAPI and the hub IOCTLs, which
// also gives the strings of a phone that is bound to another vendor's driver; everything else uses libusb.
// HEADUNIT_USB_BACKEND=libusb selects libusb on Windows as well (for comparing the two).
std::unique_ptr<IUsbBackend> CreateUsbBackend(Logger& logger)
{
#ifdef _WIN32
    const auto choice = GetEnv("HEADUNIT_USB_BACKEND");
    if (!choice || *choice != "libusb") return std::make_unique<WindowsUsbBackend>(logger);
    logger.Write(LogLevel::Info, "USB", "HEADUNIT_USB_BACKEND=libusb: discovery through libusb instead of the Windows hub IOCTLs");
#endif
    return std::make_unique<LibusbUsbBackend>(logger);
}
}
