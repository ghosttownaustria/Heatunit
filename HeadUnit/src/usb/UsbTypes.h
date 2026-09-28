#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace headunit {
// What a discovery backend puts in place of a string the device does not supply, and of one it could not read.
inline constexpr const char* kUsbStringNotSupplied = "<not supplied>";
inline constexpr const char* kUsbStringUnavailable = "<unavailable>";

// One endpoint of a USB interface, as its descriptor describes it.
struct UsbEndpoint {
    std::uint8_t address{}, attributes{};
    std::uint16_t maxPacketSize{};
    std::uint8_t interval{};
};

// One alternate setting of a USB interface, with its endpoints.
struct UsbInterface {
    std::uint8_t number{}, alternateSetting{}, classCode{}, subclassCode{}, protocolCode{};
    std::vector<UsbEndpoint> endpoints;
};

// One configuration of a USB device.
struct UsbConfiguration {
    std::uint8_t value{};
    std::vector<UsbInterface> interfaces;
};

// A device found on the bus: where it is, its identity and strings, its descriptors, and what could not be read.
struct UsbDevice {
    std::string location;
    std::uint16_t vendorId{}, productId{};
    std::string manufacturer, product, serial;
    std::uint8_t activeConfiguration{};
    std::vector<UsbConfiguration> configurations;
    std::vector<std::string> diagnostics;
};

// The result of one look at the USB bus.
struct UsbScanResult {
    std::vector<UsbDevice> devices;
    std::vector<std::string> errors;
};

bool IsUsableUsbString(const std::string& text);
}
