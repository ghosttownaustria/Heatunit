#include "usb/ProjectionTransport.h"
#include <array>
#include <chrono>
#include <string>

namespace headunit {
namespace {
using aasdk::error::Error;
using aasdk::error::ErrorCode;
// A stalled bulk endpoint is recoverable with CLEAR_HALT; give up if it keeps stalling.
constexpr int kMaxConsecutiveStalls = 3;
constexpr std::size_t kMaxFrameSize = 65535;
constexpr unsigned kReadTimeoutMs = 100;
constexpr unsigned kWriteTimeoutMs = 200;
constexpr auto kWriteDeadline = std::chrono::seconds(10);

// The transport error for a failed libusb transfer.
Error UsbError(int result)
{
    std::string text = std::string(libusb_error_name(result)) + ": " + libusb_strerror(result);
    if (result == LIBUSB_ERROR_NO_DEVICE) text += " (phone disconnected)";
    return Error(ErrorCode::USB_TRANSFER, static_cast<std::uint32_t>(result), text);
}
}

// Reads from endpoint `input` and writes to endpoint `output` of the borrowed `handle`.
ProjectionTransport::ProjectionTransport(libusb_device_handle* handle, std::uint8_t input, std::uint8_t output)
    : m_handle(handle), m_input(input), m_output(output)
{
}

// Stops the workers before the borrowed interface can go away.
ProjectionTransport::~ProjectionTransport()
{
    stop();
}

// Idempotent and safe to call from any thread except the transport's own workers. Returns after both workers have
// finished, so no USB call is in flight.
void ProjectionTransport::stop()
{
    m_isStopped = true;
    Join();
}

// Waits for both worker threads to finish (once).
void ProjectionTransport::Join()
{
    std::call_once(m_joinOnce, [this] {
        m_reader.join();
        m_writer.join();
    });
}

// Delivers exactly `size` bytes to `promise` from the reader thread.
void ProjectionTransport::receive(std::size_t size, ReceivePromise::Pointer promise)
{
    if (size == 0 || size > kMaxFrameSize) {
        promise->reject(Error(ErrorCode::USB_TRANSFER, 0, "Invalid frame read size"));
        return;
    }
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    boost::asio::post(m_reader, [self = shared_from_this(), size, promise] { self->ReadInto(size, promise); });
}

// Writes `bytes` from the writer thread. Writes run on their own thread so that a phone that stops draining the
// accessory endpoint cannot block the protocol loop that is still reading its messages.
void ProjectionTransport::send(aasdk::common::Data bytes, SendPromise::Pointer promise)
{
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    boost::asio::post(m_writer, [self = shared_from_this(), bytes = std::move(bytes), promise]() mutable { self->WriteOut(bytes, promise); });
}

// Reader thread: reads bulk transfers until `size` bytes are buffered, then resolves the promise with them.
void ProjectionTransport::ReadInto(std::size_t size, const ReceivePromise::Pointer& promise)
{
    std::array<unsigned char, 65536> chunk{};
    int stalls = 0;
    while (m_buffer.Available() < size && !m_isStopped) {
        int transferred{};
        const auto result = libusb_bulk_transfer(m_handle, m_input, chunk.data(), static_cast<int>(chunk.size()), &transferred, kReadTimeoutMs);
        if (transferred > 0) m_buffer.Append(chunk.data(), static_cast<std::size_t>(transferred));
        if (result == LIBUSB_ERROR_PIPE && ++stalls <= kMaxConsecutiveStalls && libusb_clear_halt(m_handle, m_input) == 0) continue;
        if (result < 0 && result != LIBUSB_ERROR_TIMEOUT) {
            promise->reject(UsbError(result));
            return;
        }
        if (result == 0) stalls = 0;
    }
    if (m_isStopped) {
        promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        return;
    }
    promise->resolve(m_buffer.Take(size));
}

// Writer thread: writes all of `bytes`, giving up when the phone stops reading for ten seconds.
void ProjectionTransport::WriteOut(aasdk::common::Data& bytes, const SendPromise::Pointer& promise)
{
    std::size_t offset{};
    int stalls = 0;
    const auto deadline = std::chrono::steady_clock::now() + kWriteDeadline;
    while (offset < bytes.size() && !m_isStopped) {
        int transferred{};
        const auto result = libusb_bulk_transfer(m_handle, m_output, bytes.data() + offset,
            static_cast<int>(bytes.size() - offset), &transferred, kWriteTimeoutMs);
        offset += static_cast<std::size_t>(transferred);
        if (result == LIBUSB_ERROR_PIPE && ++stalls <= kMaxConsecutiveStalls && libusb_clear_halt(m_handle, m_output) == 0) continue;
        if (result < 0 && result != LIBUSB_ERROR_TIMEOUT) {
            promise->reject(UsbError(result));
            return;
        }
        if (result == 0) stalls = 0;
        if (offset < bytes.size() && std::chrono::steady_clock::now() > deadline) {
            promise->reject(Error(ErrorCode::USB_TRANSFER, static_cast<std::uint32_t>(LIBUSB_ERROR_TIMEOUT),
                "USB write deadline after " + std::to_string(offset) + " of " + std::to_string(bytes.size()) +
                " bytes; the phone stopped reading the accessory endpoint"));
            return;
        }
    }
    if (m_isStopped) promise->reject(Error(ErrorCode::OPERATION_ABORTED));
    else promise->resolve();
}
}
