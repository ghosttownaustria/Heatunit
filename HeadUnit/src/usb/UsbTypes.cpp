#include "usb/UsbTypes.h"

namespace headunit {
// Whether a string was read from the device, as opposed to being empty or one of the placeholders above.
bool IsUsableUsbString(const std::string& text)
{
    return !text.empty() && text.front() != '<';
}
}
