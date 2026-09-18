#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace headunit {
struct UsbEndpoint {
    std::uint8_t address{}, attributes{};
    std::uint16_t maxPacketSize{};
    std::uint8_t interval{};
};
struct UsbInterface {
    std::uint8_t number{}, alternateSetting{}, classCode{}, subclassCode{}, protocolCode{};
    std::vector<UsbEndpoint> endpoints;
};
struct UsbConfiguration {
    std::uint8_t value{};
    std::vector<UsbInterface> interfaces;
};
struct UsbDevice {
    std::string location;
    std::uint16_t vendorId{}, productId{};
    std::string manufacturer, product, serial;
    std::uint8_t activeConfiguration{};
    std::vector<UsbConfiguration> configurations;
    std::vector<std::string> diagnostics;
};
struct UsbScanResult {
    std::vector<UsbDevice> devices;
    std::vector<std::string> errors;
};
}
