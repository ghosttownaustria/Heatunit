#pragma once
#include "logging/Logger.h"
#include "usb/IUsbBackend.h"
#include <memory>

namespace headunit {
std::unique_ptr<IUsbBackend> CreateUsbBackend(Logger& logger);
}
