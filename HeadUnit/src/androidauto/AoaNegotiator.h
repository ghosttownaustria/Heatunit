#pragma once
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace headunit {
struct UsbControlResult {
    int transferred{};
    std::string error;
    bool isDisconnected{};
};
class IUsbControl {
public:
    virtual ~IUsbControl() = default;
    virtual UsbControlResult Transfer(std::uint8_t requestType, std::uint8_t request,
        std::uint16_t index, std::span<std::uint8_t> bytes) = 0;
};
// Sends AOA strings then START. The caller must subsequently verify re-enumeration.
void RequestAccessoryMode(IUsbControl& control, const std::function<void(const std::string&)>& reportStage);
}
