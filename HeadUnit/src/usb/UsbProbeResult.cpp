#include "usb/UsbProbeResult.h"

namespace headunit {
// Whether the attempt reached what it was asked for (an AOA answer, a usable accessory interface, or video).
bool IsSuccessfulProbe(UsbProbeState state)
{
    return state == UsbProbeState::AoaAvailable || state == UsbProbeState::AccessoryTransportReady || state == UsbProbeState::VideoReceived;
}
}
