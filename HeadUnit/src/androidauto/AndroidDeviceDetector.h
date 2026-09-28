#pragma once
#include "usb/UsbTypes.h"
#include <cstdint>
#include <string>

namespace headunit {
inline constexpr std::uint16_t kGoogleVendorId = 0x18d1;

// How sure the detector is that a USB device is an Android phone, from weakest to strongest evidence.
enum class AndroidEvidence { None, Candidate, AdbInterface, AccessoryMode };

// What the detector makes of one device.
struct AndroidDetection {
    AndroidEvidence evidence{AndroidEvidence::None};
    std::string reason;
    bool hasAccessoryBulkPair{};
};

bool IsAccessoryModeId(std::uint16_t vendorId, std::uint16_t productId);
AndroidDetection DetectAndroidDevice(const UsbDevice& device);
}
