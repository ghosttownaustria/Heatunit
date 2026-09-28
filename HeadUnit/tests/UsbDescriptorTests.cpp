#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "usb/UsbDescriptors.h"
#include <cstdint>
#include <stdexcept>
#include <vector>

using namespace headunit;

namespace {
// A configuration descriptor with one vendor interface and a bulk IN/OUT pair, as a phone in accessory mode has it.
const std::vector<std::uint8_t> kAccessoryDescriptor{
    9, 2, 32, 0, 1, 1, 0, 0x80, 50,
    9, 4, 0, 0, 2, 0xff, 0xff, 0, 0,
    7, 5, 0x81, 2, 0, 2, 0,
    7, 5, 0x02, 2, 0, 2, 0};

// Fails unless parsing `data` is refused.
void CheckRejected(const std::vector<std::uint8_t>& data)
{
    bool hasRejected = false;
    try {
        ParseConfiguration(data);
    } catch (const std::runtime_error&) {
        hasRejected = true;
    }
    Check(hasRejected, "Malformed configuration accepted");
}

// A valid descriptor is read with its endpoints; a truncated or inconsistent one is refused.
void TestDescriptorParsing()
{
    const auto config = ParseConfiguration(kAccessoryDescriptor);
    Check(config.interfaces.size() == 1 && config.interfaces[0].endpoints.size() == 2, "Endpoint parsing failed");
    Check(config.interfaces[0].endpoints[0].maxPacketSize == 512, "Little-endian packet size failed");
    for (std::size_t size = 0; size < kAccessoryDescriptor.size(); ++size)
        CheckRejected({kAccessoryDescriptor.begin(), kAccessoryDescriptor.begin() + static_cast<std::ptrdiff_t>(size)});
    auto invalid = kAccessoryDescriptor;
    invalid[9] = 0;
    CheckRejected(invalid);
    invalid = kAccessoryDescriptor;
    invalid[9] = 255;
    CheckRejected(invalid);
    invalid = kAccessoryDescriptor;
    invalid[10] = 5;
    CheckRejected(invalid);
}

// How sure the detector is that a device is an Android phone, and whether it is in accessory mode.
void TestAndroidEvidence()
{
    const auto config = ParseConfiguration(kAccessoryDescriptor);
    UsbDevice device;
    device.vendorId = 0x04e8;
    Check(DetectAndroidDevice(device).evidence == AndroidEvidence::Candidate, "Vendor heuristic overstated");
    device.vendorId = 0x9999;
    Check(DetectAndroidDevice(device).evidence == AndroidEvidence::None, "Unknown device classified");
    device.configurations.push_back(config);
    device.configurations[0].interfaces[0].subclassCode = 0x42;
    device.configurations[0].interfaces[0].protocolCode = 1;
    Check(DetectAndroidDevice(device).evidence == AndroidEvidence::AdbInterface, "ADB missed");
    device.vendorId = 0x18d1;
    device.productId = 0x2d01;
    device.activeConfiguration = 1;
    Check(!DetectAndroidDevice(device).hasAccessoryBulkPair, "ADB endpoints mistaken for accessory");
    device.configurations[0] = config;
    Check(DetectAndroidDevice(device).hasAccessoryBulkPair, "Accessory endpoints missed");
    device.activeConfiguration = 2;
    Check(!DetectAndroidDevice(device).hasAccessoryBulkPair, "Inactive configuration accepted");
    device.activeConfiguration = 1;
    device.configurations[0].interfaces[0].alternateSetting = 1;
    Check(!DetectAndroidDevice(device).hasAccessoryBulkPair, "Inactive alternate setting accepted");
    device.productId = 0x2d02;
    Check(DetectAndroidDevice(device).evidence != AndroidEvidence::AccessoryMode, "Audio-only AOA treated as data mode");
}
}

// USB descriptor parsing and the Android device detection built on it.
void RunUsbDescriptorTests()
{
    TestDescriptorParsing();
    TestAndroidEvidence();
}
