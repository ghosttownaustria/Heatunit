#pragma once
#include <atomic>
#include <libusb.h>
#include <mutex>
#include <aasdk/Transport/ITransport.hpp>

namespace headunit {
// Borrows the claimed interface. Join must finish before the owner releases it.
class ProjectionTransport final : public aasdk::transport::ITransport,
    public std::enable_shared_from_this<ProjectionTransport> {
public:
    ProjectionTransport(libusb_device_handle* handle, std::uint8_t input, std::uint8_t output);
    ~ProjectionTransport();
    // After stop() every receive/send is rejected with OPERATION_ABORTED instead of
    // being dropped, so callers never wait for a completion that cannot come.
    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    // Idempotent and safe to call from any thread except the transport's own workers.
    // Returns after both workers have finished, so no USB call is in flight.
    void stop() override;
    void Join();
private:
    libusb_device_handle* m_handle;
    std::uint8_t m_input, m_output;
    std::atomic_bool m_isStopped{};
    std::once_flag m_joinOnce;
    boost::asio::thread_pool m_reader{1};
    // A single writer thread keeps outgoing frames ordered while it blocks on USB.
    boost::asio::thread_pool m_writer{1};
    aasdk::common::Data m_buffer;
    std::size_t m_offset{};
};
}
