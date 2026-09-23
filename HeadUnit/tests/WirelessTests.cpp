// Wireless Android Auto without a phone: the message framing and conversation of the Bluetooth link, and the
// socket transport of the Wi-Fi link (over a socketpair). Linux only, like the code it tests.
#include "wireless/SocketTransport.h"
#include "wireless/WirelessProtocol.h"
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <boost/asio.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace headunit;
namespace aaw = aap_protobuf::aaw;

namespace {
void Require(bool isValid, const char* message) { if (!isValid) throw std::runtime_error(message); }

WifiCredentials Credentials() { return {"HEATUNIT-AA", "secret-password", "DC:A6:32:01:02:03", "10.42.0.1", 5288}; }

std::vector<std::uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }

void TestFraming()
{
    const auto bytes = EncodeWirelessMessage({7, "abc"});
    Require(bytes == std::vector<std::uint8_t>({0, 3, 0, 7, 'a', 'b', 'c'}), "A wireless message is size (2 bytes), id (2 bytes), payload, big endian");
    const auto empty = EncodeWirelessMessage({2, ""});
    Require(empty == std::vector<std::uint8_t>({0, 0, 0, 2}), "An empty message is just its header");
    const auto large = EncodeWirelessMessage({1, std::string(300, 'x')});
    Require(large.size() == 304 && large[0] == 1 && large[1] == 44, "The size of a large payload is written big endian");

    // Two messages, delivered one byte at a time: the parser waits for each to be complete.
    auto stream = EncodeWirelessMessage({2, ""});
    const auto second = EncodeWirelessMessage({7, "hello"});
    stream.insert(stream.end(), second.begin(), second.end());
    WirelessFrameParser parser;
    std::vector<WirelessMessage> messages;
    for (const auto byte : stream)
        for (auto& message : parser.Feed(&byte, 1)) messages.push_back(std::move(message));
    Require(messages.size() == 2, "Byte-wise input produced the wrong number of messages");
    Require(messages[0].id == 2 && messages[0].payload.empty() && messages[1].id == 7 && messages[1].payload == "hello", "Byte-wise input changed a message");

    // The same two messages in one piece, plus the start of a third.
    WirelessFrameParser whole;
    auto burst = stream;
    burst.push_back(0);
    burst.push_back(5);
    const auto parsed = whole.Feed(burst.data(), burst.size());
    Require(parsed.size() == 2, "A partial third message must not be delivered");
    const auto rest = Bytes(std::string("\x00\x06" "abcde", 7));
    Require(whole.Feed(rest.data(), rest.size()).size() == 1, "The completed third message was not delivered");
}

const WirelessMessage* First(const WirelessHandshakeStep& step) { return step.reply.empty() ? nullptr : &step.reply.front(); }

void TestHandshake()
{
    WirelessHandshake handshake(Credentials());
    const auto start = handshake.Start();
    const auto* startMessage = First(start);
    Require(startMessage && start.reply.size() == 1 && startMessage->id == static_cast<std::uint16_t>(WirelessMessageId::StartRequest), "The conversation starts with the start request");
    aaw::WifiStartRequest request;
    Require(request.ParseFromString(startMessage->payload) && request.ip_address() == "10.42.0.1" && request.port() == 5288, "The start request names the head unit's address and port");

    const auto info = handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::InfoRequest), ""});
    const auto* infoMessage = First(info);
    Require(infoMessage && infoMessage->id == static_cast<std::uint16_t>(WirelessMessageId::InfoResponse), "The Wi-Fi details answer the info request");
    aaw::WifiInfoResponse response;
    Require(response.ParseFromString(infoMessage->payload), "The info response could not be read back");
    Require(response.ssid() == "HEATUNIT-AA" && response.password() == "secret-password" && response.bssid() == "DC:A6:32:01:02:03", "The info response carries the network");
    Require(response.security_mode() == aap_protobuf::service::wifiprojection::message::WPA2_PERSONAL, "The network is WPA2");
    Require(handshake.HasSentInfo() && !handshake.IsFailed(), "State after the info response");

    aaw::WifiStartResponse good;
    good.set_status(aaw::STATUS_SUCCESS);
    const auto accepted = handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), good.SerializeAsString()});
    Require(accepted.reply.empty() && !handshake.IsFailed(), "A successful start response ends the conversation quietly");

    aaw::WifiConnectionStatus bad;
    bad.set_status(aaw::STATUS_WIFI_INCORRECT_CREDENTIALS);
    handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::ConnectionStatus), bad.SerializeAsString()});
    Require(handshake.IsFailed() && !handshake.Failure().empty(), "A negative connection status is a failure");

    WirelessHandshake other(Credentials());
    aaw::WifiStartResponse refused;
    refused.set_status(aaw::STATUS_PHONE_WIFI_DISABLED);
    other.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), refused.SerializeAsString()});
    Require(other.IsFailed(), "A negative start response is a failure");

    WirelessHandshake quiet(Credentials());
    const auto ignored = quiet.OnMessage({99, "whatever"});
    Require(ignored.reply.empty() && !ignored.note.empty() && !quiet.IsFailed(), "An unknown message is logged and ignored");
    const auto garbage = quiet.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), "\xff\xff\xff"});
    Require(garbage.reply.empty() && !quiet.IsFailed(), "An unreadable message does not end the conversation");
}

using aasdk::common::Data;
using aasdk::error::Error;
using aasdk::error::ErrorCode;

struct Outcome {
    bool isResolved{}, isRejected{};
    ErrorCode code{};
    Data data;
};

// Runs a request on the transport and waits for its promise; five seconds are the limit.
template <class Start>
Outcome Await(Start start)
{
    boost::asio::io_context io;
    auto guard = boost::asio::make_work_guard(io);
    boost::asio::steady_timer watchdog(io, std::chrono::seconds(5));
    watchdog.async_wait([&](const boost::system::error_code& error) { if (!error) io.stop(); });
    Outcome outcome;
    const auto finished = [&] { watchdog.cancel(); guard.reset(); };
    start(io, [&](Data data) { outcome.isResolved = true; outcome.data = std::move(data); finished(); },
        [&](const Error& error) { outcome.isRejected = true; outcome.code = error.getCode(); finished(); });
    io.run();
    return outcome;
}

Outcome Receive(const std::shared_ptr<SocketTransport>& transport, std::size_t size)
{
    return Await([&](boost::asio::io_context& io, auto resolve, auto reject) {
        auto promise = aasdk::transport::ITransport::ReceivePromise::defer(io);
        promise->then(resolve, reject);
        transport->receive(size, promise);
    });
}

Outcome Send(const std::shared_ptr<SocketTransport>& transport, Data data)
{
    return Await([&](boost::asio::io_context& io, auto resolve, auto reject) {
        auto promise = aasdk::transport::ITransport::SendPromise::defer(io);
        promise->then([resolve] { resolve(Data{}); }, reject);
        transport->send(std::move(data), promise);
    });
}

void TestSocketTransport()
{
    int ends[2];
    Require(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0, "socketpair failed");
    auto transport = std::make_shared<SocketTransport>(ends[0]);

    // Bytes arriving in pieces are collected until the requested size is complete, and the surplus is kept.
    Require(::write(ends[1], "ab", 2) == 2, "write failed");
    std::thread late([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); Require(::write(ends[1], "cdef", 4) == 4, "write failed"); });
    const auto first = Receive(transport, 4);
    late.join();
    Require(first.isResolved && first.data == Bytes("abcd"), "A read that needs two arrivals returned the wrong bytes");
    const auto second = Receive(transport, 2);
    Require(second.isResolved && second.data == Bytes("ef"), "The surplus of the previous read was lost");

    // What is sent arrives in order.
    const auto sent = Send(transport, Bytes("hello"));
    Require(sent.isResolved, "Sending was not acknowledged");
    char received[8]{};
    Require(::read(ends[1], received, sizeof(received)) == 5 && std::string(received, 5) == "hello", "The peer did not get what was sent");

    // A peer that goes away is an error, not a hang.
    ::close(ends[1]);
    const auto closed = Receive(transport, 4);
    Require(closed.isRejected && closed.code == ErrorCode::TCP_TRANSFER, "A closed connection must reject the read with TCP_TRANSFER");

    // After stop() every request is rejected with OPERATION_ABORTED, and stop() may be repeated.
    transport->stop();
    transport->stop();
    const auto stopped = Receive(transport, 4);
    Require(stopped.isRejected && stopped.code == ErrorCode::OPERATION_ABORTED, "A read after stop() must be aborted");
    const auto stoppedSend = Send(transport, Bytes("x"));
    Require(stoppedSend.isRejected && stoppedSend.code == ErrorCode::OPERATION_ABORTED, "A write after stop() must be aborted");
}

// stop() must wake a read that is waiting for a phone that says nothing.
void TestStopWakesReader()
{
    int ends[2];
    Require(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0, "socketpair failed");
    auto transport = std::make_shared<SocketTransport>(ends[0]);
    std::thread stopper([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); transport->stop(); });
    const auto begin = std::chrono::steady_clock::now();
    const auto outcome = Receive(transport, 4);
    stopper.join();
    Require(outcome.isRejected && outcome.code == ErrorCode::OPERATION_ABORTED, "A waiting read must be aborted by stop()");
    Require(std::chrono::steady_clock::now() - begin < std::chrono::seconds(3), "stop() took too long to reach a waiting read");
    ::close(ends[1]);
}
}

void RunWirelessTests()
{
    TestFraming();
    TestHandshake();
    TestSocketTransport();
    TestStopWakesReader();
}
