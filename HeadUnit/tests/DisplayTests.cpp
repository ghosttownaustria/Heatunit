#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/DisplayConfig.h"
#include "androidauto/PhoneScreenDetector.h"
#include "androidauto/TouchMapping.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace headunit;

namespace {
// A press on the 800x480 picture becomes a touch at the same spot of the phone's touchscreen; the bars around it do not.
void TestTouchMapping() {
    // A 1000x400 widget shows the 800x480 video at 666x400, centred with 167 px bars left and right.
    const auto center = MapToTouch(1000, 400, 800, 480, 500, 200, false);
    Check(center && center->first == 400 && center->second == 240, "Centre of the picture is not the centre of the touchscreen");
    Check(!MapToTouch(1000, 400, 800, 480, 100, 200, false), "A press on the black bar became a touch");
    Check(!MapToTouch(1000, 400, 800, 480, 900, 200, false), "A press on the right bar became a touch");
    const auto topLeft = MapToTouch(1000, 400, 800, 480, 167, 0, false);
    Check(topLeft && topLeft->first <= 1 && topLeft->second == 0, "Top-left corner of the picture is wrong");
    const auto bottomRight = MapToTouch(1000, 400, 800, 480, 832, 399, false);
    Check(bottomRight && bottomRight->first >= kDefaultDisplay.width - 3 && bottomRight->second >= kDefaultDisplay.height - 3, "Bottom-right corner of the picture is wrong");
    // A finger that is already down keeps reporting when it leaves the picture.
    const auto clamped = MapToTouch(1000, 400, 800, 480, 5000, -50, true);
    Check(clamped && clamped->first == kDefaultDisplay.width - 1 && clamped->second == 0, "Dragging outside the picture is not clamped to its edge");
    // A tall widget letterboxes the other way.
    const auto tall = MapToTouch(400, 1000, 800, 480, 200, 500, false);
    Check(tall && tall->first == 400 && tall->second == 240, "Vertical letterboxing is wrong");
    Check(!MapToTouch(0, 400, 800, 480, 1, 1, true), "An empty widget produced a touch");
}

// The touchscreen has the size of the chosen display, not always 800x480.
void TestTouchMappingOtherDisplays() {
    const DisplayConfig hd{1280, 720}, fullHd{1920, 1080};
    const auto center = MapToTouch(1280, 720, 1280, 720, 640, 360, false, hd);
    Check(center && center->first == 640 && center->second == 360, "Centre of a 1280x720 picture is not the centre of its touchscreen");
    // A widget of half the size: the far corner is the last touch position.
    const auto corner = MapToTouch(640, 360, 1280, 720, 639.5, 359.5, false, hd);
    Check(corner && corner->first >= 1278 && corner->first <= 1279 && corner->second >= 718 && corner->second <= 719, "Bottom-right corner of a 1280x720 picture is wrong");
    const auto fullCorner = MapToTouch(960, 540, 1920, 1080, 959, 539, false, fullHd);
    Check(fullCorner && fullCorner->first >= 1917 && fullCorner->first <= 1919 && fullCorner->second >= 1077 && fullCorner->second <= 1079, "Bottom-right corner of a 1920x1080 picture is wrong");
    // A wide widget letterboxes the 16:9 picture with bars left and right.
    const auto letterboxed = MapToTouch(1000, 400, 1280, 720, 500, 200, false, hd);
    Check(letterboxed && std::abs(letterboxed->first - 640) <= 1 && std::abs(letterboxed->second - 360) <= 1, "Letterboxing a 1280x720 picture is wrong");
    Check(!MapToTouch(1000, 400, 1280, 720, 100, 200, false, hd), "A press on the black bar became a touch on a 1280x720 display");
    const auto clamped = MapToTouch(1000, 400, 1280, 720, 5000, 5000, true, hd);
    Check(clamped && clamped->first == 1279 && clamped->second == 719, "Dragging outside a 1280x720 picture is not clamped to its edge");
    // Without a display the default size is used.
    const auto standard = MapToTouch(800, 480, 800, 480, 799, 479, false);
    Check(standard && standard->first == 799 && standard->second == 479, "The default touchscreen is not 800x480");
    // The 1600x600 display rides in a 1920x1080 frame; its touchscreen is the shown 1920x720 area, not the frame.
    const DisplayConfig ultrawide{1600, 600};
    const auto wideCenter = MapToTouch(1920, 720, 1920, 720, 960, 360, false, ultrawide);
    Check(wideCenter && wideCenter->first == 960 && wideCenter->second == 360, "Centre of the 1600x600 picture is not the centre of its touchscreen");
    const auto wideCorner = MapToTouch(960, 360, 1920, 720, 959.5, 359.5, false, ultrawide);
    Check(wideCorner && wideCorner->first >= 1918 && wideCorner->first <= 1919 && wideCorner->second >= 718 && wideCorner->second <= 719, "Bottom-right corner of the 1600x600 picture is wrong");
    const auto wideLetterboxed = MapToTouch(1000, 1000, 1920, 720, 500, 500, false, ultrawide);   // shown 1000x375, 312 px bars above and below
    Check(wideLetterboxed && std::abs(wideLetterboxed->first - 960) <= 1 && std::abs(wideLetterboxed->second - 360) <= 1, "Letterboxing the 1600x600 picture is wrong");
    Check(!MapToTouch(1000, 1000, 1920, 720, 500, 100, false, ultrawide), "A press on the bar above the 1600x600 picture became a touch");
    const auto wideClamped = MapToTouch(1000, 1000, 1920, 720, 5000, 5000, true, ultrawide);
    Check(wideClamped && wideClamped->first == 1919 && wideClamped->second == 719, "Dragging outside the 1600x600 picture is not clamped to its edge");
}

// The offered displays, their densities, their text form and how the video frame carries each of them.
void TestDisplayConfig() {
    Check(std::size(kDisplays) == 4 && kDisplays[0] == DisplayConfig{800, 480} && kDisplays[1] == DisplayConfig{1280, 720} && kDisplays[2] == DisplayConfig{1600, 600} &&
        kDisplays[3] == DisplayConfig{1920, 1080}, "The offered display sizes changed");
    Check(kDefaultDisplay == DisplayConfig{800, 480}, "The default display is not 800x480");
    Check(DisplayDensity({800, 480}) == 160 && DisplayDensity({1280, 720}) == 240 && DisplayDensity({1920, 1080}) == 360, "The density does not follow the display height");
    Check(DisplayDensity({1600, 600}) == 240, "The density of the 1600x600 display does not follow its shown height (720)");
    for (const auto& display : kDisplays) Check(IsSupportedDisplay(display), "An offered display is not supported");
    Check(!IsSupportedDisplay({1024, 600}) && !IsSupportedDisplay({480, 800}) && !IsSupportedDisplay({0, 0}) && !IsSupportedDisplay({1280, 480}), "An unknown display was accepted");
    Check(DisplayText({1280, 720}) == "1280 x 720", "The display text is wrong");
    for (const char* text : {"1280x720", "1280 x 720", "1280X720", "  1280 x 720 ", "1280 X 720"})
        Check(ParseDisplay(text) == std::optional<DisplayConfig>(DisplayConfig{1280, 720}), "A valid display text was not parsed");
    Check(ParseDisplay("800x480") == std::optional<DisplayConfig>(DisplayConfig{800, 480}) && ParseDisplay("1920x1080") == std::optional<DisplayConfig>(DisplayConfig{1920, 1080}) &&
        ParseDisplay("1600x600") == std::optional<DisplayConfig>(DisplayConfig{1600, 600}) && ParseDisplay("1600 X 600") == std::optional<DisplayConfig>(DisplayConfig{1600, 600}),
        "The other display texts were not parsed");
    // How a display is carried by the frame: the smallest fixed resolution that holds it, the display fitted
    // inside with margins (sum of both sides, even), the shown area is what the head unit shows and touches.
    Check(VideoLayoutOf({800, 480}) == VideoLayout{800, 480, 0, 0, 800, 480}, "800x480 is not carried by a 800x480 frame");
    Check(VideoLayoutOf({1280, 720}) == VideoLayout{1280, 720, 0, 0, 1280, 720}, "1280x720 is not carried by a 1280x720 frame");
    Check(VideoLayoutOf({1920, 1080}) == VideoLayout{1920, 1080, 0, 0, 1920, 1080}, "1920x1080 is not carried by a 1920x1080 frame");
    const auto ultra = VideoLayoutOf({1600, 600});
    Check(ultra == VideoLayout{1920, 1080, 0, 360, 1920, 720}, "1600x600 is not fitted into the 1920x1080 frame with a 360 px height margin");
    Check(ultra.HasMargins() && ultra.Left() == 0 && ultra.Top() == 180 && !VideoLayoutOf({1280, 720}).HasMargins(), "The shown area of the 1600x600 display starts in the wrong place");
    Check(VideoLayoutOf({1024, 600}) == VideoLayout{1280, 720, 52, 0, 1228, 720}, "A display taller than its frame is not fitted with a width margin");
    Check(VideoLayoutOf({3840, 2160}) == VideoLayout{0, 0, 0, 0, 3840, 2160} && VideoLayoutOf({1920, 1200}) == VideoLayout{0, 0, 0, 0, 1920, 1200},
        "A display that no frame holds got a frame");
    Check(VideoLayoutOf({0, 0}).codecWidth == 0 && VideoLayoutOf({-800, 480}).codecWidth == 0 && VideoLayoutOf({800, 0}).codecWidth == 0, "A display without a size got a frame");
    for (const auto& display : kDisplays) {
        const auto layout = VideoLayoutOf(display);
        Check(layout.codecWidth > 0 && layout.marginWidth % 2 == 0 && layout.marginHeight % 2 == 0 && layout.width == layout.codecWidth - layout.marginWidth &&
            layout.height == layout.codecHeight - layout.marginHeight, "The layout of " + DisplayText(display) + " is inconsistent");
        Check(layout.width * display.height == layout.height * display.width, "The shown area of " + DisplayText(display) + " has another shape than the display");
    }
    for (const char* text : {"", "x", "1280", "1280x", "x720", "1024x600", "1280x720x1", "abc", "1280x720p", "-1280x720", "+1280x720", "1280,720", "720x1280", "0x0"})
        Check(!ParseDisplay(text), "An invalid display text was accepted");
    for (const auto& display : kDisplays) Check(ParseDisplay(DisplayText(display)) == std::optional<DisplayConfig>(display), "A display text does not read back");
}

// A picture of the phone, dark like Android Auto's bar, with the navigation bar button drawn in one of
// its two symbols. The shapes follow measurements of a real phone: nine 6x5 dots on a 10 px grid, or a
// 31x30 frame with a divider and a bar in its right half. Drawn in the 800x480 layout and scaled by the
// display height, like the phone scales its interface (see LayoutScale).
struct Picture {
    int width, height;
    double scale;
    std::vector<std::uint8_t> rgb;
    // The picture the head unit gets to see: the shown area of the display (for 1600x600 that is 1920x720).
    explicit Picture(const DisplayConfig& display = kDefaultDisplay)
        : width(VideoLayoutOf(display).width), height(VideoLayoutOf(display).height), scale(LayoutScale(VideoLayoutOf(display).height)),
          rgb(static_cast<std::size_t>(width) * height * 3, 0) {}
    // Sets a pixel of the picture to a grey value; pixels outside are ignored.
    void SetPixel(int x, int y, std::uint8_t value) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        for (int channel = 0; channel < 3; ++channel) rgb[(static_cast<std::size_t>(y) * width + x) * 3 + channel] = value;
    }
    // Rectangle in the 800x480 layout, both corners included.
    void Fill(int x0, int y0, int x1, int y1, std::uint8_t value = 240) {
        for (int y = static_cast<int>(y0 * scale); y < static_cast<int>((y1 + 1) * scale); ++y)
            for (int x = static_cast<int>(x0 * scale); x < static_cast<int>((x1 + 1) * scale); ++x) SetPixel(x, y, value);
    }
    // The dashboard symbol: nine dots.
    void Dots() {
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 3; ++column) Fill(31 + column * 10, 428 + row * 10, 36 + column * 10, 432 + row * 10);
    }
    // The symbol shown on any other screen: a frame with a divider and a bar.
    void Frame() {
        Fill(29, 424, 59, 426);   // top edge
        Fill(29, 451, 59, 453);   // bottom edge
        Fill(29, 424, 31, 453);   // left edge
        Fill(57, 424, 59, 453);   // right edge
        Fill(43, 424, 46, 453);   // divider
        Fill(47, 437, 59, 441);   // bar in the right half
    }
    // A focus ring around the button (radius 28 to 31 layout units). It reaches into the inspected box only at its corners.
    void Ring() {
        for (int y = static_cast<int>(400 * scale); y < height; ++y)
            for (int x = 0; x < static_cast<int>(100 * scale); ++x) {
                const double distance = std::hypot(x / scale - 44.0, y / scale - 439.0);
                if (distance >= 28.0 && distance <= 31.0) SetPixel(x, y, 240);
            }
    }
    // What the detector reads from the picture.
    PhoneScreen Detect() const { return DetectPhoneScreen(rgb.data(), width, height, width * 3); }
};

// The phone's screen is read from the symbol of its navigation bar button.
void TestPhoneScreenDetector() {
    Picture dashboard;
    dashboard.Dots();
    Check(dashboard.Detect() == PhoneScreen::Dashboard, "Nine dots were not read as the dashboard");
    Picture app;
    app.Frame();
    Check(app.Detect() == PhoneScreen::Other, "The framed symbol was not read as another screen");
    // A focus ring reaches into the box from outside and must not change the reading.
    Picture ringFrame;
    ringFrame.Frame();
    ringFrame.Ring();
    Check(ringFrame.Detect() == PhoneScreen::Other, "A focus ring around the frame changed the reading");
    Picture ringDots;
    ringDots.Dots();
    ringDots.Ring();
    Check(ringDots.Detect() == PhoneScreen::Dashboard, "A focus ring around the dots changed the reading");
    // Only the ring, without a symbol, tells nothing.
    Picture ringOnly;
    ringOnly.Ring();
    Check(ringOnly.Detect() == PhoneScreen::Unknown, "A focus ring alone was read as a known screen");
    // Every display size carries the same layout, scaled by its height (the wide ones are only wider).
    for (const auto& display : kDisplays) {
        const std::string size = DisplayText(display);
        Picture dots(display);
        dots.Dots();
        Check(dots.Detect() == PhoneScreen::Dashboard, "Dots in a " + size + " picture were misread");
        Picture frame(display);
        frame.Frame();
        Check(frame.Detect() == PhoneScreen::Other, "The frame in a " + size + " picture was misread");
        Picture ringedFrame(display);
        ringedFrame.Frame();
        ringedFrame.Ring();
        Check(ringedFrame.Detect() == PhoneScreen::Other, "A focus ring changed the reading of a " + size + " picture with the frame");
        Picture ringedDots(display);
        ringedDots.Dots();
        ringedDots.Ring();
        Check(ringedDots.Detect() == PhoneScreen::Dashboard, "A focus ring changed the reading of a " + size + " picture with the dots");
        Check(Picture(display).Detect() == PhoneScreen::Unknown, "A black " + size + " picture was read as a known screen");
        Picture ringAlone(display);
        ringAlone.Ring();
        Check(ringAlone.Detect() == PhoneScreen::Unknown, "A focus ring alone was read as a known screen in a " + size + " picture");
    }
    // A 800x480 layout drawn into a wider picture without the scaling would not be found: the layout follows the height.
    Picture unscaled(DisplayConfig{1280, 720});
    unscaled.scale = 1.0;
    unscaled.Dots();
    Check(unscaled.Detect() != PhoneScreen::Dashboard, "Dots at the 800x480 position were read in a 1280x720 picture");
    // Anything else is unknown: a black picture (splash screen, transition) and a light, flooded bar.
    Check(Picture().Detect() == PhoneScreen::Unknown, "A black picture was read as a known screen");
    Picture light;
    light.Fill(0, 400, 799, 479);
    Check(light.Detect() == PhoneScreen::Unknown, "A light bar was read as a known screen");
    Picture noise;
    noise.Fill(35, 430, 37, 432);
    noise.Fill(45, 430, 47, 432);
    Check(noise.Detect() == PhoneScreen::Unknown, "Two stray blobs were read as a known screen");
    Check(DetectPhoneScreen(nullptr, 800, 480, 2400) == PhoneScreen::Unknown, "A missing picture was read as a known screen");
    Check(DetectPhoneScreen(dashboard.rgb.data(), 100, 60, 300) == PhoneScreen::Unknown, "A tiny picture was read as a known screen");
}
}

// The display sizes, the touch mapping onto them and the reading of the phone's screen from its picture.
void RunDisplayTests()
{
    TestTouchMapping();
    TestTouchMappingOtherDisplays();
    TestDisplayConfig();
    TestPhoneScreenDetector();
}
