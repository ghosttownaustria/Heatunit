#pragma once
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <vector>

namespace headunit {
// Bounded FIFO between the protocol thread (writer) and an audio device thread (reader). When the reader falls behind,
// the oldest audio is dropped: a late sound is worse than a skipped one.
class PcmRingBuffer {
public:
    explicit PcmRingBuffer(std::size_t capacityBytes);

    void Write(std::span<const std::uint8_t> bytes);
    std::size_t Read(std::span<std::uint8_t> output);
    std::size_t Size() const;
    void Clear();
    std::uint64_t DroppedBytes() const;

private:
    mutable std::mutex m_mutex;
    std::vector<std::uint8_t> m_data;
    std::size_t m_head{};
    std::size_t m_size{};
    std::uint64_t m_dropped{};
};
}
