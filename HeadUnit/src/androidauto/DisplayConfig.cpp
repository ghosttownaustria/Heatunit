#include "androidauto/DisplayConfig.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <system_error>

namespace headunit {
// "1280 x 720"
std::string DisplayText(const DisplayConfig& display)
{
    return std::to_string(display.width) + " x " + std::to_string(display.height);
}

// The offered display that fits a screen of the given pixel size best: the one of the closest shape, and among those of
// the same shape the one closest in height. A screen without a size gets the default display.
DisplayConfig BestDisplayFor(int screenWidth, int screenHeight)
{
    if (screenWidth <= 0 || screenHeight <= 0) return kDefaultDisplay;
    constexpr double kSameShape = 1e-9;
    const double screenAspect = static_cast<double>(screenWidth) / screenHeight;
    DisplayConfig best = kDefaultDisplay;
    double bestMismatch = std::numeric_limits<double>::infinity();
    int bestHeightGap = std::numeric_limits<int>::max();
    for (const auto& display : kDisplays) {
        const double mismatch = std::abs(std::log(static_cast<double>(display.width) / display.height / screenAspect));
        const int heightGap = std::abs(display.height - screenHeight);
        const bool isBetterShape = mismatch < bestMismatch - kSameShape;
        const bool isSameShapeCloser = mismatch < bestMismatch + kSameShape && heightGap < bestHeightGap;
        if (!isBetterShape && !isSameShapeCloser) continue;
        best = display;
        bestMismatch = mismatch;
        bestHeightGap = heightGap;
    }
    return best;
}

// Reads "1280x720" (spaces are ignored, x or X); anything that is not one of `kDisplays` is refused.
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
    if (!IsSupportedDisplay(display)) return std::nullopt;
    return display;
}
}
