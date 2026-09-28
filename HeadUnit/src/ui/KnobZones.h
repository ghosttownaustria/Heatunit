#pragma once
#include "androidauto/ProjectionKeys.h"

namespace headunit {
// The round controller has five places to click: the four arrows around its rim and the centre push button. Turning is
// done by dragging around it or with the mouse wheel and needs no zone.
enum class KnobZone { None, Up, Right, Down, Left, Centre };

// Radius of the centre push button as a fraction of the controller's radius; everything between it and the rim belongs
// to the arrow in that direction.
inline constexpr double kKnobCentreRatio = 0.5;

// The key a click on an arrow sends to the phone; 0 for the other zones.
constexpr unsigned KnobZoneKey(KnobZone zone)
{
    switch (zone) {
    case KnobZone::Up: return keys::DpadUp;
    case KnobZone::Right: return keys::DpadRight;
    case KnobZone::Down: return keys::DpadDown;
    case KnobZone::Left: return keys::DpadLeft;
    default: return 0;
    }
}

KnobZone KnobZoneAt(double offsetX, double offsetY, double radius);
}
