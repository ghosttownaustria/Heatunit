#include "usb/LibusbControl.h"
#include "usb/LibusbHandles.h"

namespace headunit {
namespace {
constexpr unsigned kControlTimeoutMs = 2000;
}

// Uses `handle`, which must stay open while this object is used.
LibusbControl::LibusbControl(libusb_device_handle* handle) : m_handle(handle)
{
}

// One control transfer (value 0) with a two second timeout.
UsbControlResult LibusbControl::Transfer(std::uint8_t requestType, std::uint8_t request, std::uint16_t index, std::span<std::uint8_t> bytes)
{
    const auto result = libusb_control_transfer(m_handle, requestType, request, 0, index, bytes.data(), static_cast<std::uint16_t>(bytes.size()),
        kControlTimeoutMs);
    return {result, result < 0 ? LibusbErrorText(result) : "", result == LIBUSB_ERROR_NO_DEVICE};
}
}
