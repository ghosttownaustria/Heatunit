#pragma once
#include "logging/Logger.h"
#include "usb/UsbTypes.h"
#include <cstdint>
#include <string>

namespace headunit {
std::string UsbHex(unsigned value, int width = 4);
std::string UsbIdText(std::uint16_t vendorId, std::uint16_t productId);
void LogUsbDevice(Logger& logger, const UsbDevice& device);
void LogUsbScanSummary(Logger& logger, const UsbScanResult& result);
}
