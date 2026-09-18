#include "androidauto/AndroidDeviceDetector.h"
#include "usb/UsbDescriptors.h"
#include <iostream>
#include <stdexcept>

using namespace headunit;
void TestAoaNegotiation();
void TestAutoConnect();
void Check(bool isValid, const char* message) { if (!isValid) throw std::runtime_error(message); }
void CheckRejected(const std::vector<std::uint8_t>& data)
{
    bool hasRejected = false;
    try { ParseConfiguration(data); } catch (const std::runtime_error&) { hasRejected = true; }
    Check(hasRejected, "Malformed configuration accepted");
}
int main()
{
    try {
        TestAoaNegotiation();
        TestAutoConnect();
        const std::vector<std::uint8_t> descriptor{
            9,2,32,0,1,1,0,0x80,50,
            9,4,0,0,2,0xff,0xff,0,0,
            7,5,0x81,2,0,2,0,
            7,5,0x02,2,0,2,0};
        const auto config = ParseConfiguration(descriptor);
        Check(config.interfaces.size() == 1 && config.interfaces[0].endpoints.size() == 2, "Endpoint parsing failed");
        Check(config.interfaces[0].endpoints[0].maxPacketSize == 512, "Little-endian packet size failed");
        for (std::size_t size = 0; size < descriptor.size(); ++size)
            CheckRejected({descriptor.begin(), descriptor.begin() + size});
        auto invalid = descriptor;
        invalid[9] = 0;
        CheckRejected(invalid);
        invalid = descriptor;
        invalid[9] = 255;
        CheckRejected(invalid);
        invalid = descriptor;
        invalid[10] = 5;
        CheckRejected(invalid);
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
        std::cout << "Descriptor validation, Android evidence and AOA negotiation tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
