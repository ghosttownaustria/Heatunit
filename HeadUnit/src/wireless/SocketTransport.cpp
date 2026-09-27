#include "wireless/SocketTransport.h"
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace headunit {
using aasdk::error::Error;
using aasdk::error::ErrorCode;
namespace {
// A failure that follows stop() is the stop itself (the shutdown wakes the workers with an end-of-stream).
Error SocketError(const std::string& what, int code, bool isStopped = false)
{
    if (isStopped) return Error(ErrorCode::OPERATION_ABORTED);
    return Error(ErrorCode::TCP_TRANSFER, static_cast<std::uint32_t>(code), what + (code != 0 ? std::string(": ") + std::strerror(code) : std::string()));
}
}

SocketTransport::SocketTransport(int socketFd) : m_fd(socketFd) {}
SocketTransport::~SocketTransport()
{
    stop();
    if (m_fd >= 0) ::close(m_fd);
}
void SocketTransport::stop()
{
    m_isStopped = true;
    // Wakes a read or write that is waiting in poll(): both see the shutdown at once instead of after their timeout.
    if (m_fd >= 0) ::shutdown(m_fd, SHUT_RDWR);
    Join();
}
void SocketTransport::Join() { std::call_once(m_joinOnce, [this] { m_reader.join(); m_writer.join(); }); }

void SocketTransport::receive(std::size_t size, ReceivePromise::Pointer promise)
{
    if (size == 0 || size > 65535) { promise->reject(Error(ErrorCode::TCP_TRANSFER, 0, "Invalid frame read size")); return; }
    if (m_isStopped) { promise->reject(Error(ErrorCode::OPERATION_ABORTED)); return; }
    boost::asio::post(m_reader, [self = shared_from_this(), size, promise] {
        std::array<unsigned char, 65536> chunk{};
        while (self->m_buffer.size() - self->m_offset < size && !self->m_isStopped) {
            pollfd waiting{self->m_fd, POLLIN, 0};
            const int ready = ::poll(&waiting, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) { promise->reject(SocketError("TCP poll failed", errno, self->m_isStopped)); return; }
            if (ready == 0) continue;
            const auto got = ::recv(self->m_fd, chunk.data(), chunk.size(), 0);
            if (got > 0) { self->m_buffer.insert(self->m_buffer.end(), chunk.begin(), chunk.begin() + got); continue; }
            if (got == 0) { promise->reject(SocketError("The phone closed the wireless connection", 0, self->m_isStopped)); return; }
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
            promise->reject(SocketError("TCP read failed", errno, self->m_isStopped));
            return;
        }
        if (self->m_isStopped) { promise->reject(Error(ErrorCode::OPERATION_ABORTED)); return; }
        aasdk::common::Data data(self->m_buffer.begin() + static_cast<std::ptrdiff_t>(self->m_offset),
            self->m_buffer.begin() + static_cast<std::ptrdiff_t>(self->m_offset + size));
        self->m_offset += size;
        if (self->m_offset >= 65536 || self->m_offset == self->m_buffer.size()) {
            self->m_buffer.erase(self->m_buffer.begin(), self->m_buffer.begin() + static_cast<std::ptrdiff_t>(self->m_offset));
            self->m_offset = 0;
        }
        promise->resolve(std::move(data));
    });
}

void SocketTransport::send(aasdk::common::Data bytes, SendPromise::Pointer promise)
{
    if (m_isStopped) { promise->reject(Error(ErrorCode::OPERATION_ABORTED)); return; }
    // Own thread, like the USB transport: a phone that stops reading must not block the loop that receives.
    boost::asio::post(m_writer, [self = shared_from_this(), bytes = std::move(bytes), promise]() mutable {
        std::size_t offset{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (offset < bytes.size() && !self->m_isStopped) {
            pollfd waiting{self->m_fd, POLLOUT, 0};
            const int ready = ::poll(&waiting, 1, 200);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) { promise->reject(SocketError("TCP poll failed", errno, self->m_isStopped)); return; }
            if (ready > 0) {
                const auto sent = ::send(self->m_fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
                if (sent > 0) offset += static_cast<std::size_t>(sent);
                else if (sent < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) { promise->reject(SocketError("TCP write failed", errno, self->m_isStopped)); return; }
            }
            if (offset < bytes.size() && std::chrono::steady_clock::now() > deadline) {
                promise->reject(SocketError("TCP write deadline after " + std::to_string(offset) + " of " + std::to_string(bytes.size()) +
                    " bytes; the phone stopped reading", 0));
                return;
            }
        }
        if (self->m_isStopped) promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        else promise->resolve();
    });
}

int ListenTcp(std::uint16_t port, std::string& error)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) { error = std::string("Socket: ") + std::strerror(errno); return -1; }
    const int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, 1) != 0) {
        error = "Port " + std::to_string(port) + " laesst sich nicht oeffnen: " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

int AcceptTcp(int listenFd, int timeoutMs, std::string& peer)
{
    pollfd waiting{listenFd, POLLIN, 0};
    const int ready = ::poll(&waiting, 1, timeoutMs);
    if (ready <= 0) return -1;
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const int fd = ::accept4(listenFd, reinterpret_cast<sockaddr*>(&address), &length, SOCK_CLOEXEC);
    if (fd < 0) return -1;
    const int on = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    char text[INET_ADDRSTRLEN]{};
    peer = inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)) ? text : "?";
    return fd;
}
}
