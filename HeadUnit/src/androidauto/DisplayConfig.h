#pragma once
#include <cctype>
#include <charconv>
#include <string>
#include <string_view>
#include <optional>
#include <system_error>
#include <utility>

namespace headunit {
// The display of the simulated headunit, as the user picks it (`kDisplays`). It is announced to the phone
// once when a connection starts and cannot change while one is running.
struct DisplayConfig {
    int width{800};
    int height{480};
    friend bool operator==(const DisplayConfig&, const DisplayConfig&) = default;
};

inline constexpr DisplayConfig kDisplays[] = {{800, 480}, {1280, 720}, {1600, 600}, {1920, 1080}};
inline constexpr DisplayConfig kDefaultDisplay = kDisplays[0];

// Android Auto only knows a few fixed video resolutions (the smallest three; the larger ones are not used).
inline constexpr std::pair<int, int> kCodecResolutions[] = {{800, 480}, {1280, 720}, {1920, 1080}};

// How a display is carried by the video stream. The phone encodes a frame of one of the fixed resolutions
// (`codec`). A display of another shape is fitted into that frame: the phone lays its interface out in the
// centre area only and leaves the rest black (the video configuration's width and height margin, the sum of
// both sides; measured on a phone: the interface sits centred in the frame, with black bars above and below
// for a display wider than the frame). The head unit shows just the centre area, and touch coordinates are
// pixels of that area, not of the whole frame.
struct VideoLayout {
    int codecWidth{}, codecHeight{};       // size of the decoded frame; 0 for a display without a fitting resolution
    int marginWidth{}, marginHeight{};     // unused part of the frame, half on each side
    int width{}, height{};                 // the shown area: the picture and the touchscreen
    constexpr bool HasMargins() const { return marginWidth != 0 || marginHeight != 0; }
    // Where the shown area starts in the decoded frame.
    constexpr int Left() const { return marginWidth / 2; }
    constexpr int Top() const { return marginHeight / 2; }
    friend bool operator==(const VideoLayout&, const VideoLayout&) = default;
};

// The smallest fixed resolution that holds the display, with the display fitted inside it (sizes made even so
// that both sides of a margin are equal). A display without a fitting resolution gets no codec and no margins.
constexpr VideoLayout VideoLayoutOf(const DisplayConfig& display)
{
    if (display.width <= 0 || display.height <= 0) return VideoLayout{0, 0, 0, 0, display.width, display.height};
    for (const auto& [codecWidth, codecHeight] : kCodecResolutions) {
        if (codecWidth < display.width || codecHeight < display.height) continue;
        VideoLayout layout{codecWidth, codecHeight, 0, 0, codecWidth, codecHeight};
        const long long wide = static_cast<long long>(display.width) * codecHeight;
        const long long tall = static_cast<long long>(codecWidth) * display.height;
        if (wide > tall) {          // wider than the frame: full width, less height
            layout.height = static_cast<int>(static_cast<long long>(codecWidth) * display.height / display.width) & ~1;
            layout.marginHeight = codecHeight - layout.height;
        } else if (wide < tall) {   // taller than the frame: full height, less width
            layout.width = static_cast<int>(static_cast<long long>(codecHeight) * display.width / display.height) & ~1;
            layout.marginWidth = codecWidth - layout.width;
        }
        return layout;
    }
    return VideoLayout{0, 0, 0, 0, display.width, display.height};
}

// Screen density (dpi) announced with the video. The phone lays its interface out in density independent
// units; growing the density with the height of the shown area keeps everything the size it has on the
// smallest display, only sharper, so the picture reads the same at every resolution (and the layout that Home
// relies on stays put, see DashboardButtonPosition).
constexpr int DisplayDensity(const DisplayConfig& display) { return 160 * VideoLayoutOf(display).height / 480; }

constexpr bool IsSupportedDisplay(const DisplayConfig& display)
{
    for (const auto& known : kDisplays) if (known == display) return true;
    return false;
}

// "1280 x 720"
inline std::string DisplayText(const DisplayConfig& display)
{
    return std::to_string(display.width) + " x " + std::to_string(display.height);
}

// Reads "1280x720" (spaces are ignored, x or X); anything that is not one of `kDisplays` is refused.
inline std::optional<DisplayConfig> ParseDisplay(std::string_view text)
{
    std::string compact;
    for (const char character : text)
        if (character != ' ') compact.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
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
