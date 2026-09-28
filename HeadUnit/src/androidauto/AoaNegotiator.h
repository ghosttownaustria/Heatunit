#pragma once
#include "androidauto/IUsbControl.h"
#include <cstdint>
#include <functional>
#include <string>

namespace headunit {
// The Android Open Accessory control requests.
inline constexpr std::uint8_t kAoaVendorOut = 0x40;
inline constexpr std::uint8_t kAoaVendorIn = 0xc0;
inline constexpr std::uint8_t kAoaGetProtocol = 51;
inline constexpr std::uint8_t kAoaSendString = 52;
inline constexpr std::uint8_t kAoaStart = 53;

void RequestAccessoryMode(IUsbControl& control, const std::function<void(const std::string&)>& reportStage);
}
