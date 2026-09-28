#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/TransportReceiveBuffer.h"
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

using namespace headunit;

namespace {
// `count` bytes counting up from `first`.
std::vector<std::uint8_t> Sequence(std::size_t count, std::uint8_t first = 0)
{
    std::vector<std::uint8_t> bytes(count);
    std::iota(bytes.begin(), bytes.end(), first);
    return bytes;
}

// Received chunks are handed out in the sizes asked for, in order, across chunk borders.
void TestTakeAcrossChunks()
{
    TransportReceiveBuffer buffer;
    Check(buffer.Available() == 0, "An empty buffer reports bytes");
    const auto first = Sequence(5, 0);
    const auto second = Sequence(5, 5);
    buffer.Append(first.data(), first.size());
    buffer.Append(second.data(), second.size());
    Check(buffer.Available() == 10, "Appended bytes were not counted");
    Check(buffer.Take(3) == Sequence(3, 0), "The first bytes were not handed out first");
    Check(buffer.Take(4) == Sequence(4, 3), "Bytes across the chunk border were handed out wrongly");
    Check(buffer.Available() == 3 && buffer.Take(3) == Sequence(3, 7) && buffer.Available() == 0, "The rest was not handed out");
}

// Nothing is lost or reordered when much data passes through and the buffer compacts its storage.
void TestLongRun()
{
    TransportReceiveBuffer buffer;
    // Enough traffic to make the buffer compact several times; every byte must come out where it went in.
    const auto block = Sequence(1000);
    std::size_t streamPosition = 0;
    const auto checkInOrder = [&](const std::vector<std::uint8_t>& taken) {
        for (const auto byte : taken) Check(byte == block[streamPosition++ % block.size()], "Bytes were lost or reordered while the buffer compacted");
    };
    for (int round = 0; round < 300; ++round) {
        buffer.Append(block.data(), block.size());
        const auto taken = buffer.Take(999);
        Check(taken.size() == 999, "A block was handed out short");
        checkInOrder(taken);
    }
    Check(buffer.Available() == 300, "The buffer lost count of the bytes it holds");
    checkInOrder(buffer.Take(300));
    Check(buffer.Available() == 0, "The buffer kept bytes it handed out");
}
}

// The receive buffer the USB and TCP transports share.
void RunTransportBufferTests()
{
    TestTakeAcrossChunks();
    TestLongRun();
}
