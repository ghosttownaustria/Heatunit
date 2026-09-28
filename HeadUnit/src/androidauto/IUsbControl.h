#pragma once
#include <cstdint>
#include <span>
#include <string>

namespace headunit {
// The outcome of one control transfer: bytes transferred, or an error text, and whether the device went away.
struct UsbControlResult {
    int transferred{};
    std::string error;
    bool isDisconnected{};
};

// Sends vendor control requests to a device; the AOA negotiation talks to the phone through it, so tests can script it.
class IUsbControl {
public:
    virtual ~IUsbControl() = default;

    // One control transfer with value 0; `bytes` is sent (OUT requests) or filled (IN requests).
    virtual UsbControlResult Transfer(std::uint8_t requestType, std::uint8_t request, std::uint16_t index, std::span<std::uint8_t> bytes) = 0;
};
}
