#include "androidauto/PhoneScreenDetector.h"
#include <algorithm>
#include <vector>

namespace headunit {
namespace {
// The part of the picture that is inspected, in pixels. The symbols span x 29..59 and y 424..453 of the 800x480
// layout; the box leaves a margin so that only things reaching in from outside (a focus ring) touch its border.
struct InspectedBox {
    int left, top, width, height;
};

// The inspected box for a picture of `height` pixels, scaled like the phone's layout.
InspectedBox BoxForHeight(int height)
{
    const double scale = LayoutScale(height);
    const int left = static_cast<int>(22 * scale);
    const int right = static_cast<int>(66 * scale);
    const int top = static_cast<int>(418 * scale);
    const int bottom = static_cast<int>(460 * scale);
    return {left, top, right - left + 1, bottom - top + 1};
}

// Which pixels of the box are bright (luminance above 150); `brightCount` receives how many are.
std::vector<std::uint8_t> BrightPixels(const std::uint8_t* rgb, int stride, const InspectedBox& box, int& brightCount)
{
    std::vector<std::uint8_t> isBright(static_cast<std::size_t>(box.width) * box.height);
    brightCount = 0;
    for (int y = 0; y < box.height; ++y) {
        for (int x = 0; x < box.width; ++x) {
            const std::uint8_t* pixel = rgb + static_cast<std::size_t>(box.top + y) * stride + static_cast<std::size_t>(box.left + x) * 3;
            const int luminance = (299 * pixel[0] + 587 * pixel[1] + 114 * pixel[2]) / 1000;
            const bool isBrightPixel = luminance > 150;
            isBright[static_cast<std::size_t>(y) * box.width + x] = isBrightPixel;
            brightCount += isBrightPixel;
        }
    }
    return isBright;
}

// The sizes of the connected bright regions (8-neighbourhood) that lie fully inside the box, in 800x480 pixels; regions
// smaller than three such pixels are noise and left out.
std::vector<int> RegionSizes(const std::vector<std::uint8_t>& isBright, int boxWidth, int boxHeight, double areaScale)
{
    std::vector<int> sizes;
    std::vector<std::uint8_t> isSeen(isBright.size(), 0);
    std::vector<int> stack;
    for (int start = 0; start < boxWidth * boxHeight; ++start) {
        if (!isBright[start] || isSeen[start]) continue;
        int size = 0;
        bool isTouchingBorder = false;
        isSeen[start] = 1;
        stack.push_back(start);
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            ++size;
            const int currentX = current % boxWidth;
            const int currentY = current / boxWidth;
            if (currentX == 0 || currentY == 0 || currentX == boxWidth - 1 || currentY == boxHeight - 1) isTouchingBorder = true;
            for (int offsetY = -1; offsetY <= 1; ++offsetY) {
                for (int offsetX = -1; offsetX <= 1; ++offsetX) {
                    const int neighbourX = currentX + offsetX;
                    const int neighbourY = currentY + offsetY;
                    if (neighbourX < 0 || neighbourY < 0 || neighbourX >= boxWidth || neighbourY >= boxHeight) continue;
                    const int neighbour = neighbourY * boxWidth + neighbourX;
                    if (!isBright[neighbour] || isSeen[neighbour]) continue;
                    isSeen[neighbour] = 1;
                    stack.push_back(neighbour);
                }
            }
        }
        if (!isTouchingBorder && size >= 3 * areaScale) sizes.push_back(static_cast<int>(size / areaScale + 0.5));
    }
    return sizes;
}
}

// Where the dashboard button is, in touchscreen coordinates of `display` (pixels of its shown area).
std::pair<int, int> DashboardButtonPosition(const DisplayConfig& display)
{
    const double scale = LayoutScale(VideoLayoutOf(display).height);
    return {static_cast<int>(kDashboardButtonX * scale + 0.5), static_cast<int>(kDashboardButtonY * scale + 0.5)};
}

// Reads the dashboard button's symbol from an RGB888 picture of the phone (any of the display sizes). This is the only
// way to learn where the phone is: the protocol never reports the screen. The symbol is told apart by its shape (nine
// small separate dots against one large connected frame), which does not depend on colours; a focus ring around the
// button reaches in from outside the inspected box and is ignored.
PhoneScreen DetectPhoneScreen(const std::uint8_t* rgb, int width, int height, int stride)
{
    if (!rgb || width < 200 || height < 120) return PhoneScreen::Unknown;
    const InspectedBox box = BoxForHeight(height);
    if (box.width < 8 || box.height < 8 || box.left + box.width > width || box.top + box.height > height) return PhoneScreen::Unknown;
    int brightCount = 0;
    const auto isBright = BrightPixels(rgb, stride, box, brightCount);
    // A mostly bright box is a light theme or a full-screen flash: the shapes below would be inverted. (The framed
    // symbol alone covers about 40 percent of the box.)
    if (brightCount > box.width * box.height * 3 / 5) return PhoneScreen::Unknown;
    const double scale = LayoutScale(height);
    const auto sizes = RegionSizes(isBright, box.width, box.height, scale * scale);
    if (sizes.empty()) return PhoneScreen::Unknown;
    const int largest = *std::max_element(sizes.begin(), sizes.end());
    if (sizes.size() >= 6 && sizes.size() <= 12 && largest <= 80) return PhoneScreen::Dashboard;   // nine dots
    if (sizes.size() <= 3 && largest >= 100) return PhoneScreen::Other;                             // one frame
    return PhoneScreen::Unknown;
}
}
