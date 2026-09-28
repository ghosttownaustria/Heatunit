#include "wireless/SocketTransport.h"
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace headunit {
namespace {
using aasdk::error::Error;
using aasdk::error::ErrorCode;
constexpr std::size_t kMaxFrameSize = 65535;
constexpr int kReadPollMs = 100;
constexpr int kWritePollMs = 200;
constexpr auto kWriteDeadline = std::chrono::seconds(10);

// The transport error for a failed socket call. A failure that follows stop() is the stop itself (the shutdown wakes
// the workers with an end of the stream).
Error SocketError(const std::string& what, int code, bool isStopped = false)
{
    if (isStopped) return Error(ErrorCode::OPERATION_ABORTED);
    return Error(ErrorCode::TCP_TRANSFER, static_cast<std::uint32_t>(code), what + (code != 0 ? std::string(": ") + std::strerror(code) : std::string()));
}

// Whether a socket call only has to be repeated.
bool IsRetryable(int error)
{
    return error == EINTR || error == EAGAIN || error == EWOULDBLOCK;
}
}

// Takes ownership of a connected socket.
SocketTransport::SocketTransport(int socketFd) : m_fd(socketFd)
{
}

// Stops the workers, then closes the socket.
SocketTransport::~SocketTransport()
{
    stop();
    if (m_fd >= 0) ::close(m_fd);
}

// Idempotent; returns after both worker threads have finished. The shutdown wakes a read or write that is waiting in
// poll(): both see it at once instead of after their timeout.
void SocketTransport::stop()
{
    m_isStopped = true;
    if (m_fd >= 0) ::shutdown(m_fd, SHUT_RDWR);
    Join();
}

// Delivers exactly `size` bytes to `promise` from the reader thread.
void SocketTransport::receive(std::size_t size, ReceivePromise::Pointer promise)
{
    if (size == 0 || size > kMaxFrameSize) {
        promise->reject(Error(ErrorCode::TCP_TRANSFER, 0, "Invalid frame read size"));
        return;
    }
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    boost::asio::post(m_reader, [self = shared_from_this(), size, promise] { self->ReadInto(size, promise); });
}

// Writes `bytes` from the writer thread, like the USB transport: a phone that stops reading must not block the loop
// that receives.
void SocketTransport::send(aasdk::common::Data bytes, SendPromise::Pointer promise)
{
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    boost::asio::post(m_writer, [self = shared_from_this(), bytes = std::move(bytes), promise] { self->WriteOut(bytes, promise); });
}

// Waits for both worker threads to finish (once).
void SocketTransport::Join()
{
    std::call_once(m_joinOnce, [this] {
        m_reader.join();
        m_writer.join();
    });
}

// Reader thread: reads until `size` bytes are buffered, then resolves the promise with them.
void SocketTransport::ReadInto(std::size_t size, const ReceivePromise::Pointer& promise)
{
    std::array<std::uint8_t, 65536> chunk{};
    while (m_buffer.Available() < size && !m_isStopped) {
        pollfd waiting{m_fd, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, kReadPollMs);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) {
            promise->reject(SocketError("TCP poll failed", errno, m_isStopped));
            return;
        }
        if (ready == 0) continue;
        const auto got = ::recv(m_fd, chunk.data(), chunk.size(), 0);
        if (got > 0) {
            m_buffer.Append(chunk.data(), static_cast<std::size_t>(got));
            continue;
        }
        if (got == 0) {
            promise->reject(SocketError("The phone closed the wireless connection", 0, m_isStopped));
            return;
        }
        if (IsRetryable(errno)) continue;
        promise->reject(SocketError("TCP read failed", errno, m_isStopped));
        return;
    }
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    promise->resolve(m_buffer.Take(size));
}

// Writer thread: writes all of `bytes`, giving up when the phone stops reading for ten seconds.
void SocketTransport::WriteOut(const aasdk::common::Data& bytes, const SendPromise::Pointer& promise)
{
    std::size_t offset{};
    const auto deadline = std::chrono::steady_clock::now() + kWriteDeadline;
    while (offset < bytes.size() && !m_isStopped) {
        pollfd waiting{m_fd, POLLOUT, 0};
        const int ready = ::poll(&waiting, 1, kWritePollMs);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) {
            promise->reject(SocketError("TCP poll failed", errno, m_isStopped));
            return;
        }
        if (ready > 0) {
            const auto sent = ::send(m_fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
            } else if (sent < 0 && !IsRetryable(errno)) {
                promise->reject(SocketError("TCP write failed", errno, m_isStopped));
                return;
            }
        }
        if (offset < bytes.size() && std::chrono::steady_clock::now() > deadline) {
            promise->reject(SocketError("TCP write deadline after " + std::to_string(offset) + " of " + std::to_string(bytes.size()) +
                " bytes; the phone stopped reading", 0));
            return;
        }
    }
    if (m_isStopped) promise->reject(Error(ErrorCode::OPERATION_ABORTED));
    else promise->resolve();
}
}
