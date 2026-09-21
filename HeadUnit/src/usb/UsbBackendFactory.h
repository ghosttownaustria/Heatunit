#pragma once
#include "logging/Logger.h"
#include "usb/IUsbBackend.h"
#include <memory>

namespace headunit {
// The discovery backend of this platform. Windows reads the hub descriptors through SetupAPI and the hub
// IOCTLs, which also gives the strings of a phone that is bound to another vendor's driver; everything
// else uses libusb. HEADUNIT_USB_BACKEND=libusb selects libusb on Windows as well (for comparing the two).
std::unique_ptr<IUsbBackend> CreateUsbBackend(Logger& logger);
}
