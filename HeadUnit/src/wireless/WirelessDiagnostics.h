#pragma once
#include "logging/Logger.h"
#include <chrono>

namespace headunit {
int RunBluetoothTest(Logger& logger, std::chrono::seconds duration);
int RunHotspotTest(Logger& logger, std::chrono::seconds duration);
}
