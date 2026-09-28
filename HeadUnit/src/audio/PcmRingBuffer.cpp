#include "audio/PcmRingBuffer.h"
#include <algorithm>
#include <cstring>

namespace headunit {
// A buffer that holds up to `capacityBytes`.
PcmRingBuffer::PcmRingBuffer(std::size_t capacityBytes) : m_data(capacityBytes)
{
}

// Appends `bytes`, dropping the oldest audio where it does not fit (and all of it plus the head of `bytes` when
// `bytes` alone is larger than the buffer).
void PcmRingBuffer::Write(std::span<const std::uint8_t> bytes)
{
    std::lock_guard lock(m_mutex);
    if (bytes.size() >= m_data.size()) {
        m_dropped += m_size + bytes.size() - m_data.size();
        bytes = bytes.last(m_data.size());
        m_head = 0;
        m_size = 0;
    }
    const std::size_t overflow = m_size + bytes.size() > m_data.size() ? m_size + bytes.size() - m_data.size() : 0;
    if (overflow > 0) {
        m_head = (m_head + overflow) % m_data.size();
        m_size -= overflow;
        m_dropped += overflow;
    }
    const std::size_t tail = (m_head + m_size) % m_data.size();
    const std::size_t first = std::min(bytes.size(), m_data.size() - tail);
    std::memcpy(m_data.data() + tail, bytes.data(), first);
    std::memcpy(m_data.data(), bytes.data() + first, bytes.size() - first);
    m_size += bytes.size();
}

// Moves up to `output.size()` bytes out of the buffer; returns how many.
std::size_t PcmRingBuffer::Read(std::span<std::uint8_t> output)
{
    std::lock_guard lock(m_mutex);
    const std::size_t count = std::min(output.size(), m_size);
    const std::size_t first = std::min(count, m_data.size() - m_head);
    std::memcpy(output.data(), m_data.data() + m_head, first);
    std::memcpy(output.data() + first, m_data.data(), count - first);
    m_head = (m_head + count) % m_data.size();
    m_size -= count;
    return count;
}

// Bytes waiting to be read.
std::size_t PcmRingBuffer::Size() const
{
    std::lock_guard lock(m_mutex);
    return m_size;
}

// Drops everything that waits.
void PcmRingBuffer::Clear()
{
    std::lock_guard lock(m_mutex);
    m_head = 0;
    m_size = 0;
}

// Bytes dropped so far because the reader fell behind.
std::uint64_t PcmRingBuffer::DroppedBytes() const
{
    std::lock_guard lock(m_mutex);
    return m_dropped;
}
}
