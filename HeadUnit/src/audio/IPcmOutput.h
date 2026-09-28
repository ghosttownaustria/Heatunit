#pragma once
#include "audio/PcmFormat.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>

namespace headunit {
// Where one audio stream goes. Write runs on the protocol thread (or the radio's player) and must not block.
class IPcmOutput {
public:
    virtual ~IPcmOutput() = default;

    // Queues interleaved PCM for the sound device.
    virtual void Write(std::span<const std::uint8_t> pcm) = 0;
    // Drops what is queued (the stream stopped).
    virtual void Flush() = 0;
    // Bytes written but not yet handed to the sound device. A source faster than real time (a file decoder) waits while
    // this is high instead of overrunning the queue.
    virtual std::size_t Queued() const = 0;
};

// Opens the output for one stream; nullptr when there is none.
using AudioOpener = std::function<std::shared_ptr<IPcmOutput>(AudioKind, const PcmFormat&)>;
}
