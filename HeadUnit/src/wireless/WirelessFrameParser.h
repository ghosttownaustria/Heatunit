#pragma once
#include "wireless/WirelessProtocol.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace headunit {
// Bytes of the Bluetooth link in, complete messages out: a message may arrive in pieces, and several may arrive at once.
class WirelessFrameParser {
public:
    std::vector<WirelessMessage> Feed(const std::uint8_t* data, std::size_t size);

private:
    std::vector<std::uint8_t> m_buffer;
};
}
