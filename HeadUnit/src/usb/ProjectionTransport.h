#pragma once
#include "androidauto/TransportReceiveBuffer.h"
#include <atomic>
#include <libusb.h>
#include <mutex>
#include <aasdk/Transport/ITransport.hpp>

namespace headunit {
// The Android Auto byte stream over the phone's claimed accessory interface (borrowed from the caller, who must keep it
// claimed until Join has finished). One reader thread, one writer thread, short reads are buffered, and after stop()
// every request is rejected with OPERATION_ABORTED instead of being dropped, so callers never wait for a completion
// that cannot come.
class ProjectionTransport final : public aasdk::transport::ITransport, public std::enable_shared_from_this<ProjectionTransport> {
public:
    ProjectionTransport(libusb_device_handle* handle, std::uint8_t input, std::uint8_t output);
    ~ProjectionTransport() override;

    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    void stop() override;
    void Join();

private:
    libusb_device_handle* m_handle;
    std::uint8_t m_input;
    std::uint8_t m_output;
    std::atomic_bool m_isStopped{};
    std::once_flag m_joinOnce;
    boost::asio::thread_pool m_reader{1};
    // A single writer thread keeps outgoing frames ordered while it blocks on USB.
    boost::asio::thread_pool m_writer{1};
    TransportReceiveBuffer m_buffer;   // reader thread only

    void ReadInto(std::size_t size, const ReceivePromise::Pointer& promise);
    void WriteOut(aasdk::common::Data& bytes, const SendPromise::Pointer& promise);
};
}
