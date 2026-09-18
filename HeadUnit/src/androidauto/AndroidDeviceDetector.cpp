#include "androidauto/AndroidDeviceDetector.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace headunit {
AndroidDetection DetectAndroidDevice(const UsbDevice& device)
{
    // Audio-only AOA PIDs 2d02/2d03 do not expose an accessory data channel.
    const bool isAccessory = device.vendorId == 0x18d1 &&
        (device.productId == 0x2d00 || device.productId == 0x2d01 ||
         device.productId == 0x2d04 || device.productId == 0x2d05);
    bool hasAdb = false;
    bool hasBulkPair = false;
    for (const auto& configuration : device.configurations) {
        for (const auto& interface : configuration.interfaces) {
            const bool isAdb = interface.classCode == 0xff && interface.subclassCode == 0x42 && interface.protocolCode == 1;
            hasAdb = hasAdb || isAdb;
            bool hasIn = false, hasOut = false;
            for (const auto& endpoint : interface.endpoints) {
                if ((endpoint.attributes & 3) != 2) continue;
                if ((endpoint.address & 0x80) != 0) hasIn = true;
                else hasOut = true;
            }
            if (configuration.value == device.activeConfiguration && interface.alternateSetting == 0 &&
                interface.number == 0 && !isAdb && hasIn && hasOut) hasBulkPair = true;
        }
    }
    if (isAccessory) return {AndroidEvidence::AccessoryMode, "Android accessory data mode; Android Auto capability unverified", hasBulkPair};
    if (hasAdb) return {AndroidEvidence::AdbInterface, "ADB interface ff/42/01; Android device likely, smartphone and AA capability unverified", false};
    auto text = device.manufacturer + " " + device.product;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    constexpr std::array<std::uint16_t, 10> vendors{0x18d1, 0x04e8, 0x22b8, 0x0bb4, 0x12d1, 0x2717, 0x2a70, 0x1004, 0x0fce, 0x2d95};
    if (text.find("android") != std::string::npos || std::find(vendors.begin(), vendors.end(), device.vendorId) != vendors.end())
        return {AndroidEvidence::Candidate, "Android candidate (name/vendor heuristic only; may be a non-phone device)", false};
    return {AndroidEvidence::None, "No Android evidence; charge-only or unknown devices may remain unidentified", false};
}
}
