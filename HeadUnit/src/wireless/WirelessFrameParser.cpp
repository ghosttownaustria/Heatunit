#include "wireless/WirelessFrameParser.h"
#include <utility>

namespace headunit {
namespace {
constexpr std::size_t kHeaderSize = 4;
}

// Adds received bytes and returns every message they completed; a partial message waits for the rest.
std::vector<WirelessMessage> WirelessFrameParser::Feed(const std::uint8_t* data, std::size_t size)
{
    m_buffer.insert(m_buffer.end(), data, data + size);
    std::vector<WirelessMessage> messages;
    std::size_t offset = 0;
    while (m_buffer.size() - offset >= kHeaderSize) {
        const std::size_t payloadSize = (static_cast<std::size_t>(m_buffer[offset]) << 8) | m_buffer[offset + 1];
        if (m_buffer.size() - offset < kHeaderSize + payloadSize) break;
        WirelessMessage message;
        message.id = static_cast<std::uint16_t>((m_buffer[offset + 2] << 8) | m_buffer[offset + 3]);
        const auto begin = m_buffer.begin() + static_cast<std::ptrdiff_t>(offset + kHeaderSize);
        message.payload.assign(begin, begin + static_cast<std::ptrdiff_t>(payloadSize));
        messages.push_back(std::move(message));
        offset += kHeaderSize + payloadSize;
    }
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offset));
    return messages;
}
}
