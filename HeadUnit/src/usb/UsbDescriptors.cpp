#include "usb/UsbDescriptors.h"
#include <stdexcept>

namespace headunit {
UsbConfiguration ParseConfiguration(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() < 9 || bytes[0] < 9 || bytes[1] != 2)
        throw std::runtime_error("Invalid USB configuration header");
    const auto total = static_cast<std::size_t>(bytes[2] | (bytes[3] << 8));
    if (total < 9 || total > bytes.size())
        throw std::runtime_error("Truncated USB configuration");
    UsbConfiguration configuration;
    configuration.value = bytes[5];
    for (std::size_t offset = 0; offset < total;) {
        if (total - offset < 2 || bytes[offset] < 2 || bytes[offset] > total - offset)
            throw std::runtime_error("Invalid USB descriptor length");
        const auto descriptor = bytes.subspan(offset, bytes[offset]);
        if (descriptor[1] == 4) {
            if (descriptor.size() < 9) throw std::runtime_error("Truncated interface descriptor");
            configuration.interfaces.push_back({descriptor[2], descriptor[3], descriptor[5], descriptor[6], descriptor[7], {}});
        } else if (descriptor[1] == 5) {
            if (descriptor.size() < 7 || configuration.interfaces.empty())
                throw std::runtime_error("Invalid endpoint descriptor");
            configuration.interfaces.back().endpoints.push_back({descriptor[2], descriptor[3],
                static_cast<std::uint16_t>(descriptor[4] | (descriptor[5] << 8)), descriptor[6]});
        }
        offset += descriptor.size();
    }
    return configuration;
}
}
