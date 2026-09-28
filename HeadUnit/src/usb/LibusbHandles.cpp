#include "usb/LibusbHandles.h"

namespace headunit {
// Ends the libusb session.
void LibusbContextDeleter::operator()(libusb_context* context) const
{
    libusb_exit(context);
}

// Frees the list and drops the references to its devices.
void LibusbDeviceListDeleter::operator()(libusb_device** devices) const
{
    libusb_free_device_list(devices, 1);
}

// Closes the device.
void LibusbHandleDeleter::operator()(libusb_device_handle* handle) const
{
    libusb_close(handle);
}

// Frees the descriptor.
void LibusbConfigDeleter::operator()(libusb_config_descriptor* config) const
{
    libusb_free_config_descriptor(config);
}

// "LIBUSB_ERROR_ACCESS (-3)": a libusb error code for log lines and messages.
std::string LibusbErrorText(int code)
{
    return std::string(libusb_error_name(code)) + " (" + std::to_string(code) + ")";
}
}
