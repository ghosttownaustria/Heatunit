#include "video/VideoDecodeWorker.h"
#include <exception>
#include <utility>

namespace headunit {
namespace {
// Enough to smooth out a slow frame, little enough to keep the picture close to the phone's.
constexpr std::size_t kMaxQueuedPackets = 4;
}

// Starts the decode thread; `decode` is called only from it, `onFailure` after each packet it could not decode.
VideoDecodeWorker::VideoDecodeWorker(DecodeFunction decode, FailureHandler onFailure)
    : m_decode(std::move(decode)), m_onFailure(std::move(onFailure))
{
    m_thread = std::thread([this] { Run(); });
}

// Drops what is still queued and waits for the packet being decoded.
VideoDecodeWorker::~VideoDecodeWorker()
{
    {
        std::lock_guard lock(m_mutex);
        m_isStopping = true;
        m_queue.clear();
    }
    m_hasWork.notify_all();
    m_hasRoom.notify_all();
    m_thread.join();
}

// Queues `packet` for decoding; waits while the queue is full. Ignored once the worker is being destroyed.
void VideoDecodeWorker::Submit(std::vector<std::uint8_t> packet)
{
    std::unique_lock lock(m_mutex);
    m_hasRoom.wait(lock, [this] { return m_isStopping || m_queue.size() < kMaxQueuedPackets; });
    if (m_isStopping) return;
    m_queue.push_back(std::move(packet));
    lock.unlock();
    m_hasWork.notify_one();
}

// Returns when every submitted packet has been decoded.
void VideoDecodeWorker::WaitUntilIdle()
{
    std::unique_lock lock(m_mutex);
    m_isIdle.wait(lock, [this] { return m_queue.empty() && !m_isDecoding; });
}

// The decode thread: takes packets in order until the worker is destroyed.
void VideoDecodeWorker::Run()
{
    for (;;) {
        std::vector<std::uint8_t> packet;
        {
            std::unique_lock lock(m_mutex);
            m_hasWork.wait(lock, [this] { return m_isStopping || !m_queue.empty(); });
            if (m_isStopping) return;
            packet = std::move(m_queue.front());
            m_queue.pop_front();
            m_isDecoding = true;
        }
        m_hasRoom.notify_one();
        DecodeOne(packet);
        {
            std::lock_guard lock(m_mutex);
            m_isDecoding = false;
        }
        m_isIdle.notify_all();
    }
}

// Decodes one packet; a failure is reported and counted, a success ends the run of failures.
void VideoDecodeWorker::DecodeOne(const std::vector<std::uint8_t>& packet)
{
    try {
        m_decode(packet);
        m_consecutiveFailures = 0;
    } catch (const std::exception& error) {
        ++m_consecutiveFailures;
        if (m_onFailure) m_onFailure(error.what(), m_consecutiveFailures);
    }
}
}
