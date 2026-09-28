#include "usb/RepairResult.h"

namespace headunit {
// Whether the phone can be used afterwards: repaired, fine already, or in accessory mode (which needs no repair).
bool RepairResult::IsUsable() const
{
    return outcome == RepairOutcome::Fixed || outcome == RepairOutcome::AlreadyOk || outcome == RepairOutcome::InAccessoryMode;
}
}
