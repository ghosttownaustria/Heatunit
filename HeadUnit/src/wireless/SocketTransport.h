#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <aasdk/Transport/ITransport.hpp>

namespace headunit {
// The wireless counterpart of ProjectionTransport: the Android Auto byte stream over the TCP connection the phone
// opened. Same contract: one reader thread, one writer thread, short reads are buffered, and after stop() every
// request is rejected with OPERATION_ABORTED instead of being dropped.
class SocketTransport final : public aasdk::transport::ITransport, public std::enable_shared_from_this<SocketTransport> {
public:
    // Takes ownership of a connected socket.
    explicit SocketTransport(int socketFd);
    ~SocketTransport();
    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    // Idempotent; returns after both worker threads have finished.
    void stop() override;
private:
    void Join();
    int m_fd;
    std::atomic_bool m_isStopped{};
    std::once_flag m_joinOnce;
    boost::asio::thread_pool m_reader{1};
    boost::asio::thread_pool m_writer{1};
    aasdk::common::Data m_buffer;
    std::size_t m_offset{};
};

// A TCP socket listening on all addresses; -1 with `error` filled when that fails. The caller closes it.
int ListenTcp(std::uint16_t port, std::string& error);
// Waits up to `timeoutMs` for a connection on `listenFd`; the accepted socket (TCP_NODELAY, close-on-exec) or -1.
// `peer` receives the phone's address.
int AcceptTcp(int listenFd, int timeoutMs, std::string& peer);
}
