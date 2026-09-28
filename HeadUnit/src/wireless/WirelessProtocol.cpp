#include "wireless/WirelessProtocol.h"
#include <stdexcept>

namespace headunit {
// A message on the wire: 2 bytes payload size, 2 bytes message id (both big endian), then the payload.
std::vector<std::uint8_t> EncodeWirelessMessage(const WirelessMessage& message)
{
    if (message.payload.size() > 0xFFFF) throw std::length_error("Wireless message payload is too large");
    const auto size = static_cast<std::uint16_t>(message.payload.size());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(4 + message.payload.size());
    bytes.push_back(static_cast<std::uint8_t>(size >> 8));
    bytes.push_back(static_cast<std::uint8_t>(size & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(message.id >> 8));
    bytes.push_back(static_cast<std::uint8_t>(message.id & 0xFF));
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}
}
