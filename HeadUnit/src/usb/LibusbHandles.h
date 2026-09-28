#pragma once
#include <libusb.h>
#include <memory>
#include <string>

namespace headunit {
// Releases a libusb context.
struct LibusbContextDeleter {
    void operator()(libusb_context* context) const;
};

// Releases a device list and the references it holds.
struct LibusbDeviceListDeleter {
    void operator()(libusb_device** devices) const;
};

// Closes an opened device.
struct LibusbHandleDeleter {
    void operator()(libusb_device_handle* handle) const;
};

// Releases a configuration descriptor.
struct LibusbConfigDeleter {
    void operator()(libusb_config_descriptor* config) const;
};

using LibusbContext = std::unique_ptr<libusb_context, LibusbContextDeleter>;
using LibusbDeviceList = std::unique_ptr<libusb_device*, LibusbDeviceListDeleter>;
using LibusbHandle = std::unique_ptr<libusb_device_handle, LibusbHandleDeleter>;
using LibusbConfig = std::unique_ptr<libusb_config_descriptor, LibusbConfigDeleter>;

std::string LibusbErrorText(int code);
}
