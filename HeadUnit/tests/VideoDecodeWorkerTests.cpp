#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "video/VideoDecodeWorker.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace headunit;

namespace {
// Packets are decoded one by one in the order they were submitted, and not on the submitting thread.
void TestDecodesInOrderOnOwnThread() {
    std::mutex mutex;
    std::vector<int> decoded;
    std::thread::id decodeThread;
    {
        VideoDecodeWorker worker(
            [&](std::span<const std::uint8_t> packet) {
                std::lock_guard lock(mutex);
                decoded.push_back(packet[0]);
                decodeThread = std::this_thread::get_id();
            },
            nullptr);
        for (int index = 0; index < 50; ++index) worker.Submit({static_cast<std::uint8_t>(index)});
        worker.WaitUntilIdle();
    }
    Check(decoded.size() == 50, "Not every packet was decoded");
    for (int index = 0; index < 50; ++index) Check(decoded[index] == index, "Packets were decoded out of order");
    Check(decodeThread != std::this_thread::get_id(), "Packets were decoded on the submitting thread");
}

// A slow decoder holds Submit back once the queue is full instead of dropping packets.
void TestFullQueuePacesTheSubmitter() {
    std::atomic_int decodedCount{};
    VideoDecodeWorker worker(
        [&](std::span<const std::uint8_t>) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            ++decodedCount;
        },
        nullptr);
    for (int index = 0; index < 20; ++index) worker.Submit({1});
    worker.WaitUntilIdle();
    Check(decodedCount == 20, "A packet was dropped by a slow decoder");
}

// Failures are reported with their running count; a good packet ends the run, and decoding goes on after a failure.
void TestFailuresAreCounted() {
    std::mutex mutex;
    std::vector<int> failureCounts;
    std::atomic_int decodedCount{};
    {
        VideoDecodeWorker worker(
            [&](std::span<const std::uint8_t> packet) {
                if (packet[0] == 0) throw std::runtime_error("bad packet");
                ++decodedCount;
            },
            [&](const std::string& reason, int consecutiveFailures) {
                Check(reason == "bad packet", "The failure reason was not passed on");
                std::lock_guard lock(mutex);
                failureCounts.push_back(consecutiveFailures);
            });
        for (const std::uint8_t packet : std::vector<std::uint8_t>{0, 0, 1, 0, 1}) worker.Submit({packet});
        worker.WaitUntilIdle();
    }
    Check(decodedCount == 2, "Decoding did not go on after a failure");
    Check((failureCounts == std::vector<int>{1, 2, 1}), "The failures were not counted as runs");
}

// Destroying a worker with packets still queued does not hang and does not decode what is left.
void TestDestroyWithQueuedPackets() {
    std::atomic_int decodedCount{};
    {
        VideoDecodeWorker worker(
            [&](std::span<const std::uint8_t>) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                ++decodedCount;
            },
            nullptr);
        for (int index = 0; index < 4; ++index) worker.Submit({1});
    }
    Check(decodedCount < 4, "The destroyed worker still decoded its whole queue");
}
}

// The decode thread of the video pipeline.
void RunVideoDecodeWorkerTests() {
    TestDecodesInOrderOnOwnThread();
    TestFullQueuePacesTheSubmitter();
    TestFailuresAreCounted();
    TestDestroyWithQueuedPackets();
}
