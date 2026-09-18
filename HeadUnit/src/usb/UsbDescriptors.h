#pragma once
#include "usb/UsbTypes.h"
#include <span>

namespace headunit {
UsbConfiguration ParseConfiguration(std::span<const std::uint8_t> bytes);
}
