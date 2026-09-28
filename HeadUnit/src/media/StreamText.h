#pragma once
#include <string>
#include <string_view>

namespace headunit {
bool IsValidUtf8(std::string_view text);
std::string Latin1ToUtf8(std::string_view text);
std::string IcyStreamTitle(std::string_view packet);
std::string FormatPlayTime(double seconds);
}
