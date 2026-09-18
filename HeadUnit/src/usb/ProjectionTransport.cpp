#include "usb/ProjectionTransport.h"
#include <array>
#include <chrono>

namespace headunit {
using aasdk::error::Error;
using aasdk::error::ErrorCode;
ProjectionTransport::ProjectionTransport(libusb_device_handle* handle, std::uint8_t input, std::uint8_t output)
    : m_handle(handle), m_input(input), m_output(output) {}
ProjectionTransport::~ProjectionTransport() { stop(); Join(); }
void ProjectionTransport::stop() { m_isStopped = true; Join(); }
void ProjectionTransport::Join() { m_reader.join(); m_writer.join(); }
void ProjectionTransport::receive(std::size_t size, ReceivePromise::Pointer promise) {
    if (size == 0 || size > 65535) { promise->reject(Error(ErrorCode::USB_TRANSFER, 0, "Invalid frame read size")); return; }
    boost::asio::post(m_reader, [self = shared_from_this(), size, promise] {
        std::array<unsigned char, 65536> chunk{};
        while (self->m_buffer.size() - self->m_offset < size && !self->m_isStopped) {
            int transferred{};
            const auto result = libusb_bulk_transfer(self->m_handle, self->m_input, chunk.data(), static_cast<int>(chunk.size()), &transferred, 100);
            if (transferred > 0) self->m_buffer.insert(self->m_buffer.end(), chunk.begin(), chunk.begin() + transferred);
            if (result < 0 && result != LIBUSB_ERROR_TIMEOUT) {
                promise->reject(Error(ErrorCode::USB_TRANSFER, result, libusb_error_name(result))); return;
            }
        }
        if (self->m_isStopped) { promise->reject(Error(ErrorCode::OPERATION_ABORTED)); return; }
        aasdk::common::Data data(self->m_buffer.begin() + self->m_offset, self->m_buffer.begin() + self->m_offset + size);
        self->m_offset += size;
        if (self->m_offset >= 65536 || self->m_offset == self->m_buffer.size()) {
            self->m_buffer.erase(self->m_buffer.begin(), self->m_buffer.begin() + self->m_offset);
            self->m_offset = 0;
        }
        promise->resolve(std::move(data));
    });
}
void ProjectionTransport::send(aasdk::common::Data bytes, SendPromise::Pointer promise) {
    // Writes run on their own thread so a phone that stops draining the accessory
    // endpoint cannot block the protocol loop that is still reading its messages.
    boost::asio::post(m_writer, [self = shared_from_this(), bytes = std::move(bytes), promise]() mutable {
        std::size_t offset{};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (offset < bytes.size() && !self->m_isStopped) {
            int transferred{};
            const auto result = libusb_bulk_transfer(self->m_handle, self->m_output, bytes.data() + offset,
                static_cast<int>(bytes.size() - offset), &transferred, 200);
            offset += transferred;
            if (result < 0 && result != LIBUSB_ERROR_TIMEOUT) {
                promise->reject(Error(ErrorCode::USB_TRANSFER, result, libusb_error_name(result))); return;
            }
            if (offset < bytes.size() && std::chrono::steady_clock::now() > deadline) {
                promise->reject(Error(ErrorCode::USB_TRANSFER, static_cast<std::uint32_t>(LIBUSB_ERROR_TIMEOUT),
                    "USB write deadline after " + std::to_string(offset) + " of " + std::to_string(bytes.size())
                    + " bytes; the phone stopped reading the accessory endpoint"));
                return;
            }
        }
        if (self->m_isStopped) promise->reject(Error(ErrorCode::OPERATION_ABORTED));
        else promise->resolve();
    });
}
}
