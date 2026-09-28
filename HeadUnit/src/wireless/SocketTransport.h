#pragma once
#include "androidauto/TransportReceiveBuffer.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <aasdk/Transport/ITransport.hpp>

namespace headunit {
// The wireless counterpart of ProjectionTransport: the Android Auto byte stream over the TCP connection the phone
// opened. Same contract: one reader thread, one writer thread, short reads are buffered, and after stop() every request
// is rejected with OPERATION_ABORTED instead of being dropped.
class SocketTransport final : public aasdk::transport::ITransport, public std::enable_shared_from_this<SocketTransport> {
public:
    explicit SocketTransport(int socketFd);
    ~SocketTransport() override;

    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    void stop() override;

private:
    int m_fd;
    std::atomic_bool m_isStopped{};
    std::once_flag m_joinOnce;
    boost::asio::thread_pool m_reader{1};
    boost::asio::thread_pool m_writer{1};
    TransportReceiveBuffer m_buffer;   // reader thread only

    void Join();
    void ReadInto(std::size_t size, const ReceivePromise::Pointer& promise);
    void WriteOut(const aasdk::common::Data& bytes, const SendPromise::Pointer& promise);
};
}
