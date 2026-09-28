#pragma once
#include "androidauto/DisplayConfig.h"
#include <optional>
#include <utility>

namespace headunit {
std::optional<std::pair<int, int>> MapToTouch(int widgetWidth, int widgetHeight, int videoWidth, int videoHeight, double x, double y,
    bool isClamped, const DisplayConfig& display = kDefaultDisplay);
}
