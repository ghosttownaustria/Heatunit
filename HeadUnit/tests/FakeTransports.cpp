#include "FakeTransports.h"
#include <utility>

// Fails the read as aborted.
void StoppedTransport::receive(std::size_t /*size*/, ReceivePromise::Pointer promise)
{
    promise->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
}

// Accepts the bytes.
void StoppedTransport::send(aasdk::common::Data /*bytes*/, SendPromise::Pointer promise)
{
    promise->resolve();
}

// Notes that the session stopped the transport.
void StoppedTransport::stop()
{
    m_hasStopped = true;
}

// Whether the session stopped the transport.
bool StoppedTransport::HasStopped() const
{
    return m_hasStopped;
}

// Keeps the read pending; only stop() ends it.
void SilentTransport::receive(std::size_t /*size*/, ReceivePromise::Pointer promise)
{
    m_pending = std::move(promise);
}

// Accepts the bytes.
void SilentTransport::send(aasdk::common::Data /*bytes*/, SendPromise::Pointer promise)
{
    promise->resolve();
}

// Fails the pending read as aborted, as a real transport does.
void SilentTransport::stop()
{
    m_hasStopped = true;
    if (m_pending) std::exchange(m_pending, nullptr)->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
}

// Whether the session stopped the transport.
bool SilentTransport::HasStopped() const
{
    return m_hasStopped;
}
