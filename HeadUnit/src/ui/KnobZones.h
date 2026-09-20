#pragma once
#include "androidauto/ProjectionInput.h"
#include <cmath>

namespace headunit {
// The round controller has five places to click: the four arrows around its rim and the centre push
// button. Turning is done by dragging around it or with the mouse wheel and needs no zone.
enum class KnobZone { None, Up, Right, Down, Left, Centre };

// Radius of the centre push button as a fraction of the controller's radius; everything between it and
// the rim belongs to the arrow in that direction.
constexpr double kKnobCentreRatio = 0.5;

// Which zone the position `dx`,`dy` (pixels from the controller's centre, y grows downwards) is in for a
// controller of `radius` pixels. The arrows share the rim in four 90 degree sectors; outside the circle is
// no zone.
inline KnobZone KnobZoneAt(double dx, double dy, double radius)
{
    if (radius <= 0) return KnobZone::None;
    const double distance = std::hypot(dx, dy);
    if (distance > radius) return KnobZone::None;
    if (distance < radius * kKnobCentreRatio) return KnobZone::Centre;
    if (std::abs(dx) > std::abs(dy)) return dx > 0 ? KnobZone::Right : KnobZone::Left;
    return dy > 0 ? KnobZone::Down : KnobZone::Up;
}

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
}
