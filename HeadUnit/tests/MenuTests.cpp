#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/ConsoleController.h"
#include "androidauto/ProjectionKeys.h"
#include "ui/BluetoothPhones.h"
#include "ui/HomeMenuEntry.h"
#include "ui/HomeMenuLayout.h"
#include "ui/HomeTileSetup.h"
#include "ui/KnobZones.h"
#include "ui/PageFocus.h"
#include "ui/StatusBar.h"
#include "ui/TouchDrag.h"
#include "ui/VolumeBar.h"
#include <cmath>
#include <string>
#include <vector>

using namespace headunit;

namespace {
using Screen = ConsoleController::Screen;

// The round controller: four arrows on its rim, a push button in the middle, nothing outside.
void TestKnobZones() {
    constexpr double radius = 100;
    Check(KnobZoneAt(0, 0, radius) == KnobZone::Centre, "The middle of the controller is not its push button");
    Check(KnobZoneAt(0, -80, radius) == KnobZone::Up && KnobZoneAt(80, 0, radius) == KnobZone::Right &&
        KnobZoneAt(0, 80, radius) == KnobZone::Down && KnobZoneAt(-80, 0, radius) == KnobZone::Left, "The arrows are not where they are drawn");
    // The push button ends at half the radius.
    Check(KnobZoneAt(0, -49, radius) == KnobZone::Centre && KnobZoneAt(0, -51, radius) == KnobZone::Up &&
        KnobZoneAt(-49, 0, radius) == KnobZone::Centre && KnobZoneAt(-51, 0, radius) == KnobZone::Left, "The push button has the wrong size");
    // The arrows share the rim in four 90 degree sectors: the larger distance from the middle wins.
    Check(KnobZoneAt(60, -50, radius) == KnobZone::Right && KnobZoneAt(50, -60, radius) == KnobZone::Up &&
        KnobZoneAt(50, 60, radius) == KnobZone::Down && KnobZoneAt(60, 50, radius) == KnobZone::Right &&
        KnobZoneAt(-50, 60, radius) == KnobZone::Down && KnobZoneAt(-60, 50, radius) == KnobZone::Left &&
        KnobZoneAt(-60, -50, radius) == KnobZone::Left && KnobZoneAt(-50, -60, radius) == KnobZone::Up, "The sectors of the arrows are wrong");
    // Rim included, everything beyond it is no zone; so is a controller without a size.
    Check(KnobZoneAt(0, -100, radius) == KnobZone::Up && KnobZoneAt(0, -101, radius) == KnobZone::None &&
        KnobZoneAt(75, 75, radius) == KnobZone::None && KnobZoneAt(1, 1, 0) == KnobZone::None && KnobZoneAt(1, 1, -5) == KnobZone::None,
        "A click outside the controller hit a zone");
    // All the way round: an arrow with a key on the rim, the push button (no key) inside.
    for (int degrees = 0; degrees < 360; degrees += 5) {
        const double radians = degrees * 3.14159265358979323846 / 180.0;
        const KnobZone rim = KnobZoneAt(std::sin(radians) * 80, -std::cos(radians) * 80, radius);
        const KnobZone inside = KnobZoneAt(std::sin(radians) * 30, -std::cos(radians) * 30, radius);
        Check(rim != KnobZone::None && rim != KnobZone::Centre && KnobZoneKey(rim) != 0, "A point on the rim is not an arrow");
        Check(inside == KnobZone::Centre && KnobZoneKey(inside) == 0, "A point near the middle is not the push button");
    }
    Check(KnobZoneKey(KnobZone::Up) == keys::DpadUp && KnobZoneKey(KnobZone::Right) == keys::DpadRight &&
        KnobZoneKey(KnobZone::Down) == keys::DpadDown && KnobZoneKey(KnobZone::Left) == keys::DpadLeft, "An arrow sends the wrong key");
    Check(KnobZoneKey(KnobZone::Centre) == 0 && KnobZoneKey(KnobZone::None) == 0, "The middle or nothing sends an arrow key");
}

// The radio's home menu: the tiles of the designs plus Android Auto, laid out as in the 1600x600 design, the focused tile
// in the middle as far as the row allows, arrows at the edges where more tiles follow and the bar at the bottom.
void TestHomeMenuLayout() {
    using E = HomeMenuEntry;
    Check(kHomeMenuCount == 8 && std::string(HomeMenuTitle(kHomeMenuEntries[0])) == "Android Auto" &&
        std::string(HomeMenuTitle(kHomeMenuEntries[6])) == "Bluetooth" && std::string(HomeMenuTitle(kHomeMenuEntries[7])) == "Settings" &&
        std::string(HomeMenuId(E::AndroidAuto)) == "AndroidAuto" && std::string(HomeMenuId(E::Vehicle)) == "Vehicle" &&
        std::string(HomeMenuId(E::Bluetooth)) == "Bluetooth", "The home menu does not have the tiles of the design");
    // What a tile opens: a page of the radio or what a controller key does; Vehicle has nothing behind it yet.
    Check(HomeMenuPage(E::Multimedia) == Screen::Multimedia && HomeMenuPage(E::Radio) == Screen::Radio && HomeMenuPage(E::Settings) == Screen::Settings &&
        HomeMenuPage(E::Bluetooth) == Screen::Bluetooth && !HomeMenuKey(E::Bluetooth) &&
        HomeMenuKey(E::AndroidAuto) == ConsoleKey::Projection && HomeMenuKey(E::Telephone) == ConsoleKey::Tel &&
        HomeMenuKey(E::Navigation) == ConsoleKey::Nav && !HomeMenuPage(E::AndroidAuto) && !HomeMenuKey(E::Multimedia) &&
        !HomeMenuKey(E::Radio) && !HomeMenuKey(E::Settings) && !HomeMenuPage(E::Telephone) && !HomeMenuKey(E::Vehicle) &&
        !HomeMenuPage(E::Vehicle), "A home menu tile opens the wrong thing");
    // The screen is 600 units high and as wide as the display's shape.
    Check(HomeMenuWidth({1600, 600}) == 1600 && HomeMenuWidth(kDefaultDisplay) == 1000 && std::abs(HomeMenuWidth({1920, 1080}) - 1066.67) < 0.01 &&
        HomeMenuWidth({0, 0}) == 0, "The home menu has the wrong width");
    Check(HomeTileLeft(0) == 25 && HomeTileLeft(4) == 1225 && HomeRowWidth(6) == 1800 && HomeRowWidth(7) == 2100 && HomeRowWidth(0) == 0,
        "The tiles are not where the design has them");
    // The design's six tiles on 1600x600: the first three keep the row at its start (the design's picture), the last
    // three at its end.
    for (int focus = 0; focus <= 2; ++focus) Check(HomeMenuScroll(focus, 1600, 6) == 0, "The design's first screen scrolled");
    for (int focus = 3; focus <= 5; ++focus) Check(HomeMenuScroll(focus, 1600, 6) == 200, "The end of the row did not stop the scrolling");
    // Seven tiles on 800x480: in between, the focused tile is exactly in the middle.
    Check(HomeMenuScroll(0, 1000, 7) == 0 && HomeMenuScroll(1, 1000, 7) == 0 && HomeMenuScroll(2, 1000, 7) == 250 &&
        HomeMenuScroll(3, 1000, 7) == 550 && HomeMenuScroll(4, 1000, 7) == 850 && HomeMenuScroll(5, 1000, 7) == 1100 &&
        HomeMenuScroll(6, 1000, 7) == 1100, "The focused tile is not kept in the middle on 800x480");
    Check(HomeMenuScroll(5, 2400, 6) == 0, "A row that fits scrolled");
    // On every display and with any number of tiles, every focused tile is fully in view and clear of the edge strips.
    for (const auto& display : kDisplays) {
        const double width = HomeMenuWidth(display);
        for (int count = 1; count <= kHomeMenuCount; ++count) {
            for (int focus = 0; focus < count; ++focus) {
                const double scroll = HomeMenuScroll(focus, width, count);
                const double left = HomeTileLeft(focus) - scroll, right = left + kHomeTileWidth;
                const HomeEdges edges = HomeMenuEdges(scroll, width, count);
                Check(scroll >= 0 && scroll <= std::max(0.0, HomeRowWidth(count) - width) && left >= 0 && right <= width &&
                    (!edges.isLeft || left >= kHomeEdgeWidth + kHomeEdgeFade) && (!edges.isRight || right <= width - kHomeEdgeWidth - kHomeEdgeFade),
                    "A focused tile is hidden on a " + DisplayText(display) + " display");
            }
        }
    }
    // Arrows: only where the row goes on.
    Check(!HomeMenuEdges(0, 1600, 6).isLeft && HomeMenuEdges(0, 1600, 6).isRight && HomeMenuEdges(200, 1600, 6).isLeft &&
        !HomeMenuEdges(200, 1600, 6).isRight && HomeMenuEdges(100, 1600, 6).isLeft && HomeMenuEdges(100, 1600, 6).isRight &&
        !HomeMenuEdges(0, 2400, 6).isLeft && !HomeMenuEdges(0, 2400, 6).isRight, "The edge arrows are wrong");
    // The bar: as in the design at the start of six tiles on 1600x600 (lit from 25 to about 1403), at the end of the row
    // it reaches the right end of the track; the whole track when the row fits.
    const HomeBar start = HomeMenuBar(0, 1600, 6), end = HomeMenuBar(200, 1600, 6), all = HomeMenuBar(0, 2400, 6);
    Check(start.from == 25 && std::abs(start.to - 1403.5) < 1 && std::abs(end.from - 197.2) < 0.1 && end.to == 1575 && all.from == 25 && all.to == 2375,
        "The bar at the bottom shows the wrong part");
    // Clicks: on a tile, in the gaps, above and below the row, with the row scrolled, on tiles that are not there.
    Check(HomeTileAt(150, 300, 0, 6) == 0 && HomeTileAt(1350, 300, 0, 6) == 4 && HomeTileAt(1560, 300, 0, 6) == 5, "A click missed its tile");
    Check(!HomeTileAt(300, 300, 0, 6) && !HomeTileAt(10, 300, 0, 6) && !HomeTileAt(150, 50, 0, 6) && !HomeTileAt(150, 520, 0, 6) &&
        !HomeTileAt(1790, 300, 0, 6) && !HomeTileAt(1560, 300, 0, 5), "A click outside the tiles hit one");
    Check(HomeTileAt(150, 300, 200, 6) == 1 && HomeTileAt(1560, 300, 200, 6) == 5, "A click on the scrolled row hit the wrong tile");
    // The focus stops at both ends.
    Check(MoveHomeFocus(0, -1, 6) == 0 && MoveHomeFocus(0, 2, 6) == 2 && MoveHomeFocus(4, 3, 6) == 5 && MoveHomeFocus(5, -9, 6) == 0 &&
        MoveHomeFocus(3, 1, 1) == 0, "The focus left the row");
}

// Which tiles the home menu shows and in which order: shown, hidden (never Settings), moved, stored and read back.
void TestTileSetup() {
    using E = HomeMenuEntry;
    using Tiles = std::vector<HomeMenuEntry>;
    HomeTileSetup setup = DefaultTileSetup();
    Check(ShownTiles(setup) == Tiles(std::begin(kHomeMenuEntries), std::end(kHomeMenuEntries)), "A new radio does not show every tile");
    Check(SetTileShown(setup, E::Vehicle, false) && !SetTileShown(setup, E::Vehicle, false) && !SetTileShown(setup, E::Settings, false) &&
        !SetTileShown(setup, E::Radio, true), "Showing or hiding a tile is wrong");
    Check(ShownTiles(setup) == Tiles{E::AndroidAuto, E::Multimedia, E::Radio, E::Telephone, E::Navigation, E::Bluetooth, E::Settings},
        "A hidden tile is shown");
    // On the home menu a tile passes the next shown one (and the hidden ones between).
    Check(MoveTile(setup, E::Navigation, +1, true) &&
        ShownTiles(setup) == Tiles{E::AndroidAuto, E::Multimedia, E::Radio, E::Telephone, E::Bluetooth, E::Navigation, E::Settings},
        "Moving right past a hidden tile on the home menu is wrong");
    Check(MoveTile(setup, E::Navigation, +1, true) &&
        ShownTiles(setup) == Tiles{E::AndroidAuto, E::Multimedia, E::Radio, E::Telephone, E::Bluetooth, E::Settings, E::Navigation},
        "Moving right on the home menu is wrong");
    Check(!MoveTile(setup, E::Navigation, +1, true), "A tile moved beyond the end of the row");
    Check(MoveTile(setup, E::Navigation, -1, true) &&
        ShownTiles(setup) == Tiles{E::AndroidAuto, E::Multimedia, E::Radio, E::Telephone, E::Bluetooth, E::Navigation, E::Settings},
        "Moving left on the home menu is wrong");
    // In the settings list a tile passes its direct neighbour, hidden or not.
    Check(MoveTile(setup, E::Navigation, -1, false) && setup.tiles[5].entry == E::Navigation && setup.tiles[6].entry == E::Bluetooth,
        "Moving up in the settings did not pass the neighbour");
    Check(MoveTile(setup, E::Navigation, -1, false) && setup.tiles[4].entry == E::Navigation && setup.tiles[5].entry == E::Vehicle,
        "Moving up in the settings did not pass the hidden neighbour");
    Check(MoveTile(setup, E::Settings, -1, false) && MoveTile(setup, E::Settings, -1, false) && setup.tiles[5].entry == E::Settings &&
        !setup.tiles[6].isShown, "Moving past a hidden tile is wrong");
    Check(!MoveTile(setup, E::AndroidAuto, -1, false) && !MoveTile(setup, E::AndroidAuto, 0, false), "A tile moved before the start");
    // Stored as text and read back.
    Check(TileSetupText(setup) == "AndroidAuto,Multimedia,Radio,Telephone,Navigation,Settings,-Vehicle,Bluetooth", "The tiles are stored wrongly");
    Check(ParseTileSetup(TileSetupText(setup)) == setup, "The stored tiles do not read back");
    // A damaged or older text: unknown and repeated names are skipped, Settings is shown, missing tiles follow at the end.
    Check(TileSetupText(ParseTileSetup(" Radio , -Settings,Bogus,Radio,-AndroidAuto,-")) ==
        "Radio,Settings,-AndroidAuto,Multimedia,Telephone,Navigation,Vehicle,Bluetooth", "A damaged setting was read wrongly");
    Check(ParseTileSetup("") == DefaultTileSetup(), "An empty setting is not the default");
}

// The player pages: a list that scrolls with the focus. Turning moves within the part the focus is in; up and down jump
// between the list, the controls and the top buttons; left and right never move the focus.
void TestPlayerPageFocus() {
    Check(ListFirstRow(0, 0, 5, 20) == 0 && ListFirstRow(4, 0, 5, 20) == 0 && ListFirstRow(5, 0, 5, 20) == 1 && ListFirstRow(19, 0, 5, 20) == 15 &&
        ListFirstRow(10, 15, 5, 20) == 10 && ListFirstRow(12, 10, 5, 20) == 10, "The list does not follow the focus");
    Check(ListFirstRow(3, 0, 5, 3) == 0 && ListFirstRow(2, 7, 5, 3) == 0 && ListFirstRow(0, 4, 5, 0) == 0 && ListFirstRow(1, 0, 0, 5) == 0,
        "A short or empty list scrolled");
    using Part = PageFocus::Part;
    const PageShape shape{2, 3, 10};
    PageFocus focus;   // starts in the list
    focus = TurnFocus(focus, shape, 3);
    Check(focus.part == Part::List && focus.row == 3, "Turning did not move through the list");
    focus = TurnFocus(focus, shape, 50);
    Check(focus.row == 9, "Turning left the end of the list");
    Check(NudgeFocus(focus, shape, keys::DpadLeft) == focus && NudgeFocus(focus, shape, keys::DpadRight) == focus, "Left or right moved the focus");
    Check(NudgeFocus(focus, shape, keys::DpadDown) == focus, "Down in the list went somewhere");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Controls && focus.button == 1 && focus.row == 9, "Up in the list did not jump to the play button");
    focus = TurnFocus(focus, shape, -5);
    Check(focus.part == Part::Controls && focus.button == 0, "Turning left the controls");
    Check(NudgeFocus(focus, shape, keys::DpadRight) == focus, "Right moved along the controls like turning");
    focus = TurnFocus(focus, shape, 5);
    Check(focus.part == Part::Controls && focus.button == 2, "Turning did not stop at the last control");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Header && focus.button == 0, "Up from the controls did not reach the top buttons");
    focus = NudgeFocus(focus, shape, keys::DpadUp);
    Check(focus.part == Part::Header, "Up left the top of the page");
    focus = NudgeFocus(focus, shape, keys::DpadDown);
    Check(focus.part == Part::Controls && focus.button == 1, "Down from the top buttons did not reach the play button");
    focus = NudgeFocus(focus, shape, keys::DpadDown);
    Check(focus.part == Part::List && focus.row == 9, "Down did not come back to where the list was");
    // The list becomes empty (a new country loads): the focus waits on the play button; a shorter list keeps it in range.
    focus.row = 8;
    Check(FitFocus(focus, {2, 3, 0}) == PageFocus{Part::Controls, 1, 0}, "An empty list kept the focus");
    Check(FitFocus(focus, {2, 3, 4}).row == 3, "A shorter list left the focus beyond its end");
    Check(NudgeFocus(PageFocus{Part::Controls, 1, 0}, {0, 3, 4}, keys::DpadUp).part == Part::Controls, "Up went to top buttons that are not there");
    Check(NudgeFocus(PageFocus{Part::Controls, 1, 0}, {1, 3, 0}, keys::DpadDown).part == Part::Controls, "Down went into an empty list");
}

// The Bluetooth page's list: the Android Auto phone first, then the connected ones, each group by name; every phone but
// the Android Auto one can be chosen.
void TestBluetoothPhones() {
    const std::vector<BluetoothPhone> phones{{"/p/1", "zeta", false, false}, {"/p/2", "Beta", true, false}, {"/p/3", "alpha", false, false},
        {"/p/4", "Jakob's Flip 8", true, true}, {"/p/5", "Alpha", true, false}};
    const auto sorted = SortedPhones(phones);
    std::vector<std::string> names;
    for (const auto& phone : sorted) names.push_back(phone.name);
    Check(names == std::vector<std::string>{"Jakob's Flip 8", "Alpha", "Beta", "alpha", "zeta"},
        "The phones are listed in the wrong order");
    Check(PhoneStateText(sorted[0]) == "Android Auto" && PhoneStateText(sorted[1]) == "Connected" && PhoneStateText(sorted[3]) == "Not connected",
        "A phone's state reads wrongly");
    Check(!CanSwitchTo(sorted[0]) && CanSwitchTo(sorted[1]) && CanSwitchTo(sorted[4]) && !CanSwitchTo(BluetoothPhone{}),
        "The wrong phones can be chosen for Android Auto");
    Check(SortedPhones({}).empty(), "An empty list got phones");
}

// The status bar: the symbols where the design has them, measured from the right edge on every display, touch areas
// side by side, and the source's name, the radio's own player first.
void TestStatusBar() {
    const StatusBox home = StatusIconBox(StatusButton::Home, 1600);
    Check(home.left == 1532.5 && home.top == 32.5 && std::abs(StatusIconBox(StatusButton::Speaker, 1600).left - 1401.9) < 0.01 &&
        StatusIconBox(StatusButton::Microphone, 1000).left == 872, "The status bar's symbols are not where the design has them");
    for (const double width : {1000.0, 1600.0}) {
        for (const StatusButton button : {StatusButton::Speaker, StatusButton::Microphone, StatusButton::Home}) {
            const StatusBox icon = StatusIconBox(button, width);
            Check(StatusButtonAt(icon.left + icon.width / 2, icon.top + icon.height / 2, width) == button, "A symbol does not take a touch on it");
        }
        Check(StatusTouchBox(StatusButton::Speaker, width).left + StatusTouchBox(StatusButton::Speaker, width).width ==
            StatusTouchBox(StatusButton::Microphone, width).left, "The touch areas leave a gap");
        Check(!StatusButtonAt(width - 100, 150, width) && !StatusButtonAt(100, 50, width) && !StatusButtonAt(width - 5, 50, width),
            "A touch beside the symbols hit one");
        Check(StatusBarLeft(width) < StatusSourceRight(width) && StatusSourceRight(width) < StatusIconBox(StatusButton::Speaker, width).left,
            "The source's name runs into the symbols");
    }
    Check(StatusBarLeft(1600) == 975 && StatusBarLeft(1000) == 525, "The status bar takes the wrong room");
    Check(StatusSourceText("Oe3", true, "Flip") == "Oe3" && StatusSourceText("", true, "Flip") == "Flip" &&
        StatusSourceText("", true, "") == "Android Auto" && StatusSourceText("", false, "Flip").empty(), "The source of the sound is named wrongly");
}

// Touch: a press stays a tap until it moves more than the threshold; a swipe along the home row gives the focus to the
// tile in the middle, never against the swipe, and a flick moves on by one.
void TestTouch() {
    TouchDrag drag;
    Check(!drag.Move(50, 50) && !drag.IsDragging(), "A move without a press became a drag");
    drag.Press(100, 100);
    Check(!drag.Move(110, 90) && !drag.IsDragging() && drag.IsDown(), "A small wobble became a drag");
    Check(drag.Move(100, 140) && drag.IsDragging() && !drag.IsHorizontal() && drag.DeltaY() == 40 && drag.StartY() == 100, "A drag down was not seen");
    drag.Release();
    Check(drag.IsDragging() && !drag.IsDown(), "The release forgot the drag");
    drag.Press(0, 0);
    Check(!drag.IsDragging() && drag.Move(-30, 5) && drag.IsHorizontal() && drag.DeltaX() == -30, "A new press kept the old drag");
    // 1000 wide, 8 tiles: the middle tile after the swipe gets the focus.
    Check(SwipeFocus(1, HomeMenuScroll(3, 1000, 8), 1000, 8, -600) == 3, "A swipe to the left did not focus the tile in the middle");
    Check(SwipeFocus(5, HomeMenuScroll(2, 1000, 8), 1000, 8, +900) == 2, "A swipe to the right did not focus the tile in the middle");
    Check(SwipeFocus(0, 0, 1600, 8, +200) == 0 && SwipeFocus(4, HomeMenuScroll(6, 1000, 8), 1000, 8, +300) <= 4, "A swipe moved the focus against it");
    Check(SwipeFocus(2, HomeMenuScroll(2, 1000, 8) + 70, 1000, 8, -70) == 3 && SwipeFocus(2, HomeMenuScroll(2, 1000, 8) - 70, 1000, 8, +70) == 1 &&
        SwipeFocus(2, HomeMenuScroll(2, 1000, 8) + 20, 1000, 8, -20) == 2, "A flick did not move on by one, or a tiny one did");
    Check(SwipeFocus(7, HomeMenuScroll(7, 1000, 8), 1000, 8, -200) == 7 && SwipeFocus(0, 0, 1000, 0, -200) == 0, "A swipe left the row");
}

// The volume bar: centred, never wider than 1000 units nor closer than 50 to the edges, and a touch lights the segment
// under it with all before it.
void TestVolumeBar() {
    const VolumePanel wide = VolumePanelOf(1600);
    Check(wide.left == 300 && wide.right == 1300 && wide.barLeft == 410 && wide.barRight == 1180, "The bar is not centred on the wide display");
    const VolumePanel small = VolumePanelOf(1000);
    Check(small.left == 50 && small.right == 950, "The bar leaves the small display's margins");
    constexpr int max = 30;
    Check(VolumeAt(small.barLeft - 40, small, max) == 0 && VolumeAt(small.barLeft, small, max) == 0, "Left of the bar is not silence");
    const double cell = (small.barRight - small.barLeft) / max;
    Check(VolumeAt(small.barLeft + 1, small, max) == 1 && VolumeAt(small.barLeft + cell * 0.5, small, max) == 1 &&
        VolumeAt(small.barLeft + cell * 1.5, small, max) == 2 && VolumeAt(small.barLeft + cell * 14.5, small, max) == 15,
        "A touch does not light the segment under it");
    Check(VolumeAt(small.barRight - 1, small, max) == max && VolumeAt(small.barRight + 80, small, max) == max, "The end of the bar is not the maximum");
    Check(VolumeAt(500, VolumePanelOf(0), max) == 0 && VolumeAt(500, small, 0) == 0, "A bar without room set a volume");
}
}

// The radio's menu pages: the knob, the home menu's layout and tiles, the focus on the player pages, the volume bar, the
// Bluetooth page's list, the status bar and touch.
void RunMenuTests()
{
    TestKnobZones();
    TestHomeMenuLayout();
    TestTileSetup();
    TestPlayerPageFocus();
    TestVolumeBar();
    TestBluetoothPhones();
    TestStatusBar();
    TestTouch();
}
