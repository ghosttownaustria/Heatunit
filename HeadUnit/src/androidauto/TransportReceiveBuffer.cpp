#include "androidauto/TransportReceiveBuffer.h"

namespace headunit {
namespace {
// Taken bytes are removed from the front only once this many have piled up (or nothing is left), not per request.
constexpr std::size_t kCompactionThreshold = 65536;
}

// Adds received bytes at the end.
void TransportReceiveBuffer::Append(const std::uint8_t* data, std::size_t size)
{
    m_buffer.insert(m_buffer.end(), data, data + size);
}

// Bytes waiting to be taken.
std::size_t TransportReceiveBuffer::Available() const
{
    return m_buffer.size() - m_offset;
}

// Takes the next `size` bytes; the caller makes sure that many are available.
std::vector<std::uint8_t> TransportReceiveBuffer::Take(std::size_t size)
{
    const auto begin = m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset);
    std::vector<std::uint8_t> data(begin, begin + static_cast<std::ptrdiff_t>(size));
    m_offset += size;
    if (m_offset >= kCompactionThreshold || m_offset == m_buffer.size()) {
        m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(m_offset));
        m_offset = 0;
    }
    return data;
}
}
