#pragma once
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace headunit {
// Runs a packet decoder on its own thread, so that the protocol thread can go on reading from the phone and acknowledge
// its video while the previous packet is still being decoded. Packets are decoded strictly in the order they were
// submitted and are never dropped (a lost H.264 packet would smear the picture until the next keyframe); when the small
// queue is full, Submit waits, which paces the phone exactly like a slow decoder did before.
class VideoDecodeWorker {
public:
    using DecodeFunction = std::function<void(std::span<const std::uint8_t>)>;
    using FailureHandler = std::function<void(const std::string& reason, int consecutiveFailures)>;

    VideoDecodeWorker(DecodeFunction decode, FailureHandler onFailure);
    ~VideoDecodeWorker();
    VideoDecodeWorker(const VideoDecodeWorker&) = delete;
    VideoDecodeWorker& operator=(const VideoDecodeWorker&) = delete;

    void Submit(std::vector<std::uint8_t> packet);
    void WaitUntilIdle();

private:
    DecodeFunction m_decode;
    FailureHandler m_onFailure;
    std::mutex m_mutex;
    std::condition_variable m_hasWork;
    std::condition_variable m_hasRoom;
    std::condition_variable m_isIdle;
    std::deque<std::vector<std::uint8_t>> m_queue;
    bool m_isDecoding{};
    bool m_isStopping{};
    int m_consecutiveFailures{};
    std::thread m_thread;

    void Run();
    void DecodeOne(const std::vector<std::uint8_t>& packet);
};
}
