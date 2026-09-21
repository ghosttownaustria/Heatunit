#pragma once
#include "logging/Logger.h"
#include "usb/UsbTypes.h"
#include <string>

namespace headunit {
// Upper-case hexadecimal, zero padded to `width` digits.
std::string UsbHex(unsigned value, int width = 4);
// The lines every discovery backend writes for a detected device: identity, strings, descriptors and
// what the Android detector makes of it.
void LogUsbDevice(Logger& logger, const UsbDevice& device);
}
