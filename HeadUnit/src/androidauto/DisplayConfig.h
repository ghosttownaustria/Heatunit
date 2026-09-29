#pragma once
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace headunit {
// The display of the head unit in pixels, as the screen has it or the user types it (any size within the limits below).
// It is announced to the phone once when a connection starts and cannot change while one is running.
struct DisplayConfig {
    friend bool operator==(const DisplayConfig&, const DisplayConfig&) = default;

    int width{800};
    int height{480};
};

// Used when the screen has no usable size.
inline constexpr DisplayConfig kDefaultDisplay{800, 480};
// The sizes that are accepted; a display beyond the largest frame is announced scaled down (see VideoLayoutOf).
inline constexpr DisplayConfig kMinDisplay{320, 200};
inline constexpr DisplayConfig kMaxDisplay{7680, 4320};

// Android Auto only knows a few fixed video resolutions (the smallest three; the larger ones are not used).
inline constexpr std::pair<int, int> kCodecResolutions[] = {{800, 480}, {1280, 720}, {1920, 1080}};

// How a display is carried by the video stream. The phone encodes a frame of one of the fixed resolutions (`codec`). A
// display of another shape is fitted into that frame: the phone lays its interface out in the centre area only and
// leaves the rest black (the video configuration's width and height margin, the sum of both sides; measured on a phone:
// the interface sits centred in the frame, with black bars above and below for a display wider than the frame). The head
// unit shows just the centre area, and touch coordinates are pixels of that area, not of the whole frame.
struct VideoLayout {
    friend bool operator==(const VideoLayout&, const VideoLayout&) = default;

    int codecWidth{}, codecHeight{};       // size of the decoded frame; 0 for a display without a fitting resolution
    int marginWidth{}, marginHeight{};     // unused part of the frame, half on each side
    int width{}, height{};                 // the shown area: the picture and the touchscreen

    // Whether the frame is larger than the shown area.
    constexpr bool HasMargins() const { return marginWidth != 0 || marginHeight != 0; }
    // Where the shown area starts in the decoded frame, horizontally.
    constexpr int Left() const { return marginWidth / 2; }
    // Where the shown area starts in the decoded frame, vertically.
    constexpr int Top() const { return marginHeight / 2; }
};

// The size the phone lays its interface out for: the display itself, or, when it is larger than the largest frame, the
// display scaled down to that frame with the same shape (the head unit scales the picture up to the screen again).
constexpr DisplayConfig PhoneSizeOf(const DisplayConfig& display)
{
    const auto [maxWidth, maxHeight] = kCodecResolutions[std::size(kCodecResolutions) - 1];
    if (display.width <= maxWidth && display.height <= maxHeight) return display;
    const long long wide = static_cast<long long>(display.width) * maxHeight;
    const long long tall = static_cast<long long>(maxWidth) * display.height;
    if (wide > tall) return {maxWidth, static_cast<int>(static_cast<long long>(maxWidth) * display.height / display.width) & ~1};
    return {static_cast<int>(static_cast<long long>(maxHeight) * display.width / display.height) & ~1, maxHeight};
}

// The smallest fixed resolution that holds the display, with the display fitted inside it (sizes made even so that both
// sides of a margin are equal). A display of no size gets no codec and no margins.
constexpr VideoLayout VideoLayoutOf(const DisplayConfig& display)
{
    if (display.width <= 0 || display.height <= 0) return VideoLayout{0, 0, 0, 0, display.width, display.height};
    const DisplayConfig phone = PhoneSizeOf(display);
    if (phone.width <= 0 || phone.height <= 0) return VideoLayout{0, 0, 0, 0, display.width, display.height};
    for (const auto& [codecWidth, codecHeight] : kCodecResolutions) {
        if (codecWidth < phone.width || codecHeight < phone.height) continue;
        VideoLayout layout{codecWidth, codecHeight, 0, 0, codecWidth, codecHeight};
        const long long wide = static_cast<long long>(phone.width) * codecHeight;
        const long long tall = static_cast<long long>(codecWidth) * phone.height;
        if (wide > tall) {          // wider than the frame: full width, less height
            layout.height = static_cast<int>(static_cast<long long>(codecWidth) * phone.height / phone.width) & ~1;
            layout.marginHeight = codecHeight - layout.height;
        } else if (wide < tall) {   // taller than the frame: full height, less width
            layout.width = static_cast<int>(static_cast<long long>(codecHeight) * phone.width / phone.height) & ~1;
            layout.marginWidth = codecWidth - layout.width;
        }
        return layout;
    }
    return VideoLayout{0, 0, 0, 0, display.width, display.height};
}

// Screen density (dpi) announced with the video. The phone lays its interface out in density independent units;
// growing the density with the height of the shown area keeps everything the size it has on the smallest display, only
// sharper, so the picture reads the same at every resolution (and the layout that Home relies on stays put, see
// DashboardButtonPosition).
constexpr int DisplayDensity(const DisplayConfig& display)
{
    return 160 * VideoLayoutOf(display).height / 480;
}

// Whether the display is within the accepted sizes (`kMinDisplay` to `kMaxDisplay`).
constexpr bool IsValidDisplay(const DisplayConfig& display)
{
    return display.width >= kMinDisplay.width && display.height >= kMinDisplay.height && display.width <= kMaxDisplay.width &&
        display.height <= kMaxDisplay.height;
}

std::string DisplayText(const DisplayConfig& display);
DisplayConfig DisplayForScreen(int screenWidth, int screenHeight);
std::optional<DisplayConfig> ParseDisplay(std::string_view text);
}
