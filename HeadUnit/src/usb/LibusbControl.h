#pragma once
#include "androidauto/IUsbControl.h"
#include <libusb.h>

namespace headunit {
// Control transfers to an opened libusb device (borrowed, not owned), for the AOA negotiation.
class LibusbControl final : public IUsbControl {
public:
    explicit LibusbControl(libusb_device_handle* handle);

    UsbControlResult Transfer(std::uint8_t requestType, std::uint8_t request, std::uint16_t index, std::span<std::uint8_t> bytes) override;

private:
    libusb_device_handle* m_handle;
};
}
