#include "androidauto/AndroidDeviceDetector.h"
#include <algorithm>
#include <array>
#include <cctype>

namespace headunit {
namespace {
constexpr std::uint8_t kBulkTransferType = 2;
constexpr std::uint8_t kEndpointDirectionIn = 0x80;

// The ADB function of an Android device: vendor class ff, subclass 42, protocol 01.
bool IsAdbInterface(const UsbInterface& usbInterface)
{
    return usbInterface.classCode == 0xff && usbInterface.subclassCode == 0x42 && usbInterface.protocolCode == 1;
}

// Whether the interface has a bulk IN and a bulk OUT endpoint, which the accessory data channel needs.
bool HasBulkPair(const UsbInterface& usbInterface)
{
    bool hasIn = false;
    bool hasOut = false;
    for (const auto& endpoint : usbInterface.endpoints) {
        if ((endpoint.attributes & 3) != kBulkTransferType) continue;
        if ((endpoint.address & kEndpointDirectionIn) != 0) hasIn = true;
        else hasOut = true;
    }
    return hasIn && hasOut;
}

// The accessory data channel: interface 0, alternate setting 0 of the active configuration, not ADB, with a bulk pair.
bool HasAccessoryBulkPair(const UsbDevice& device)
{
    for (const auto& configuration : device.configurations) {
        if (configuration.value != device.activeConfiguration) continue;
        for (const auto& usbInterface : configuration.interfaces) {
            if (usbInterface.number == 0 && usbInterface.alternateSetting == 0 && !IsAdbInterface(usbInterface) && HasBulkPair(usbInterface))
                return true;
        }
    }
    return false;
}

// Whether any configuration of the device offers ADB.
bool HasAdbInterface(const UsbDevice& device)
{
    for (const auto& configuration : device.configurations) {
        if (std::any_of(configuration.interfaces.begin(), configuration.interfaces.end(), IsAdbInterface)) return true;
    }
    return false;
}

// A heuristic only: "android" in the device's name, or the vendor id of a phone maker.
bool LooksLikeAndroid(const UsbDevice& device)
{
    auto text = device.manufacturer + " " + device.product;
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    constexpr std::array<std::uint16_t, 10> kPhoneVendors{kGoogleVendorId, 0x04e8, 0x22b8, 0x0bb4, 0x12d1, 0x2717, 0x2a70, 0x1004, 0x0fce, 0x2d95};
    return text.find("android") != std::string::npos || std::find(kPhoneVendors.begin(), kPhoneVendors.end(), device.vendorId) != kPhoneVendors.end();
}
}

// Whether the ids are those of an Android device in accessory mode with a data channel. The audio-only accessory
// product ids 2d02/2d03 are left out: they expose no data channel.
bool IsAccessoryModeId(std::uint16_t vendorId, std::uint16_t productId)
{
    return vendorId == kGoogleVendorId && (productId == 0x2d00 || productId == 0x2d01 || productId == 0x2d04 || productId == 0x2d05);
}

// Classifies a device by the strongest evidence it shows: accessory mode, an ADB interface, or only its name or vendor.
AndroidDetection DetectAndroidDevice(const UsbDevice& device)
{
    if (IsAccessoryModeId(device.vendorId, device.productId))
        return {AndroidEvidence::AccessoryMode, "Android accessory data mode; Android Auto capability unverified", HasAccessoryBulkPair(device)};
    if (HasAdbInterface(device))
        return {AndroidEvidence::AdbInterface, "ADB interface ff/42/01; Android device likely, smartphone and AA capability unverified", false};
    if (LooksLikeAndroid(device))
        return {AndroidEvidence::Candidate, "Android candidate (name/vendor heuristic only; may be a non-phone device)", false};
    return {AndroidEvidence::None, "No Android evidence; charge-only or unknown devices may remain unidentified", false};
}
}
