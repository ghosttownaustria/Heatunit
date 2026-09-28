#pragma once
#include "androidauto/ProjectionKeys.h"
#include <algorithm>

namespace headunit {
// Where the controller is on a player page (Multimedia, Radio): a row of buttons at the top (header), the player's
// controls below the title and the list. Turning the knob and pushing its arrows do different things: turning moves
// within the part the focus is in (through the list, along a row of buttons); up and down jump from part to part (list,
// controls, top buttons) wherever in the list the focus is. Left and right do not move the focus at all (the page uses
// them to skip to the previous or next title).
struct PageFocus {
    // The parts of a player page, from top to bottom.
    enum class Part { Header, Controls, List };

    friend bool operator==(const PageFocus&, const PageFocus&) = default;

    Part part{Part::List};
    int button{};   // in the header or the controls
    int row{};      // in the list; kept while the focus is elsewhere
};

// How many buttons and rows a player page has right now.
struct PageShape {
    int headerButtons{}, controlButtons{}, rows{};
};

// The focus made valid again after the page changed (the list got shorter or empty, ...).
constexpr PageFocus FitFocus(PageFocus focus, const PageShape& shape)
{
    focus.row = shape.rows > 0 ? std::clamp(focus.row, 0, shape.rows - 1) : 0;
    if (focus.part == PageFocus::Part::List && shape.rows == 0) {
        focus.part = PageFocus::Part::Controls;
        focus.button = shape.controlButtons / 2;
    }
    if (focus.part == PageFocus::Part::Header && shape.headerButtons == 0) focus.part = PageFocus::Part::Controls;
    const int buttons = focus.part == PageFocus::Part::Header ? shape.headerButtons : shape.controlButtons;
    focus.button = std::clamp(focus.button, 0, std::max(0, buttons - 1));
    return focus;
}

// The focus after turning the knob `steps` detents: through the list, or along the row of buttons the focus is on.
constexpr PageFocus TurnFocus(PageFocus focus, const PageShape& shape, int steps)
{
    focus = FitFocus(focus, shape);
    if (focus.part == PageFocus::Part::List) focus.row = std::clamp(focus.row + steps, 0, std::max(0, shape.rows - 1));
    else focus.button += steps;
    return FitFocus(focus, shape);
}

// The focus after an arrow of the controller: up and down jump between the parts (the middle control first).
constexpr PageFocus NudgeFocus(PageFocus focus, const PageShape& shape, unsigned keycode)
{
    using Part = PageFocus::Part;
    focus = FitFocus(focus, shape);
    if (keycode == keys::DpadUp) {
        if (focus.part == Part::List) {
            focus.part = Part::Controls;
            focus.button = shape.controlButtons / 2;
        } else if (focus.part == Part::Controls && shape.headerButtons > 0) {
            focus.part = Part::Header;
            focus.button = 0;
        }
    } else if (keycode == keys::DpadDown) {
        if (focus.part == Part::Header) {
            focus.part = Part::Controls;
            focus.button = shape.controlButtons / 2;
        } else if (focus.part == Part::Controls && shape.rows > 0) {
            focus.part = Part::List;
        }
    }
    return FitFocus(focus, shape);
}

// The first row a list of `count` rows shows, `visible` at a time, so that row `focus` is in view; starting from `first`
// it moves as little as possible.
constexpr int ListFirstRow(int focus, int first, int visible, int count)
{
    if (count <= 0 || visible <= 0) return 0;
    if (focus < first) first = focus;
    if (focus >= first + visible) first = focus - visible + 1;
    return std::clamp(first, 0, std::max(0, count - visible));
}
}
