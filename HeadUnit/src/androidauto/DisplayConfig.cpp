#include "androidauto/DisplayConfig.h"
#include <cctype>
#include <charconv>
#include <system_error>

namespace headunit {
// "1280 x 720"
std::string DisplayText(const DisplayConfig& display)
{
    return std::to_string(display.width) + " x " + std::to_string(display.height);
}

// The display for a screen of the given pixel size: exactly that size. A screen without a usable size gets the default display.
DisplayConfig DisplayForScreen(int screenWidth, int screenHeight)
{
    const DisplayConfig screen{screenWidth, screenHeight};
    return IsValidDisplay(screen) ? screen : kDefaultDisplay;
}

// Reads "1280x720" (spaces are ignored, x or X); a size outside the accepted range is refused.
std::optional<DisplayConfig> ParseDisplay(std::string_view text)
{
    std::string compact;
    for (const char character : text) {
        if (character != ' ') compact.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    const auto separator = compact.find('x');
    if (separator == std::string::npos) return std::nullopt;
    DisplayConfig display;
    const char* const begin = compact.data();
    const char* const end = begin + compact.size();
    const auto width = std::from_chars(begin, begin + separator, display.width);
    if (width.ec != std::errc{} || width.ptr != begin + separator) return std::nullopt;
    const auto height = std::from_chars(begin + separator + 1, end, display.height);
    if (height.ec != std::errc{} || height.ptr != end) return std::nullopt;
    if (!IsValidDisplay(display)) return std::nullopt;
    return display;
}
}
