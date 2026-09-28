#pragma once
#include <aasdk/Transport/ITransport.hpp>

// A phone that is gone: every read fails at once as aborted, every write succeeds.
class StoppedTransport final : public aasdk::transport::ITransport {
public:
    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    void stop() override;

    bool HasStopped() const;

private:
    bool m_hasStopped{};
};

// A phone that connected but never says anything: the read stays pending until stop().
class SilentTransport final : public aasdk::transport::ITransport {
public:
    void receive(std::size_t size, ReceivePromise::Pointer promise) override;
    void send(aasdk::common::Data bytes, SendPromise::Pointer promise) override;
    void stop() override;

    bool HasStopped() const;

private:
    ReceivePromise::Pointer m_pending;
    bool m_hasStopped{};
};
