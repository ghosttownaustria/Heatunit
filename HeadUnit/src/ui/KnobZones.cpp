#include "ui/KnobZones.h"
#include <cmath>

namespace headunit {
// Which zone the position `offsetX`,`offsetY` (pixels from the controller's centre, y grows downwards) is in for a
// controller of `radius` pixels. The arrows share the rim in four 90 degree sectors; outside the circle is no zone.
KnobZone KnobZoneAt(double offsetX, double offsetY, double radius)
{
    if (radius <= 0) return KnobZone::None;
    const double distance = std::hypot(offsetX, offsetY);
    if (distance > radius) return KnobZone::None;
    if (distance < radius * kKnobCentreRatio) return KnobZone::Centre;
    if (std::abs(offsetX) > std::abs(offsetY)) return offsetX > 0 ? KnobZone::Right : KnobZone::Left;
    return offsetY > 0 ? KnobZone::Down : KnobZone::Up;
}
}
