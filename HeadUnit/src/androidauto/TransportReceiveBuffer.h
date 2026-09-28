#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace headunit {
// The bytes a transport has received from the phone and not handed out yet. Transports read in large chunks and hand
// out exactly the sizes the protocol asks for; the rest waits here for the next request. Used by one reader thread.
class TransportReceiveBuffer {
public:
    void Append(const std::uint8_t* data, std::size_t size);
    std::size_t Available() const;
    std::vector<std::uint8_t> Take(std::size_t size);

private:
    std::vector<std::uint8_t> m_buffer;
    std::size_t m_offset{};
};
}
