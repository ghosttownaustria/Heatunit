// Wireless Android Auto without a phone: the message framing and conversation of the Bluetooth link, and the
// socket transport of the Wi-Fi link (over a socketpair). Linux only, like the code it tests.
#include "logging/Logger.h"
#include "wireless/SocketTransport.h"
#include "wireless/WirelessLink.h"
#include "wireless/WirelessProtocol.h"
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <arpa/inet.h>
#include <boost/asio.hpp>
#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <netinet/in.h>
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
    // Android's numbering: WPA2 personal is 8. The imported enum's WPA2_PERSONAL (5) is unknown to the phone.
    Require(static_cast<int>(response.security_mode()) == kWifiSecurityWpa2Personal && kWifiSecurityWpa2Personal == 8, "The network must be announced as WPA2 in Android's numbering (8)");
    Require(infoMessage->payload.find(std::string("\x20\x08", 2)) != std::string::npos, "Field 4 (security mode) must be the varint 8 on the wire");
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

// A phone at the other end of the "Bluetooth" socket (a socketpair): reads what the head unit says, answers like a phone.
class FakePhone {
public:
    explicit FakePhone(int fd) : m_fd(fd) {}
    WirelessMessage Read()
    {
        while (m_pending.empty()) {
            std::uint8_t buffer[512];
            const auto got = ::recv(m_fd, buffer, sizeof(buffer), 0);
            if (got <= 0) throw std::runtime_error("the head unit closed the Bluetooth socket");
            for (auto& message : m_parser.Feed(buffer, static_cast<std::size_t>(got))) m_pending.push_back(std::move(message));
        }
        auto message = std::move(m_pending.front());
        m_pending.pop_front();
        return message;
    }
    void Send(WirelessMessageId id, const std::string& payload = {})
    {
        const auto bytes = EncodeWirelessMessage({static_cast<std::uint16_t>(id), payload});
        Require(::send(m_fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()), "the fake phone could not send");
    }
    // The usual start: the start request arrives, the phone asks for the network and gets it.
    void AskForNetwork(std::uint16_t port)
    {
        const auto start = Read();
        aaw::WifiStartRequest request;
        Require(start.id == static_cast<std::uint16_t>(WirelessMessageId::StartRequest) && request.ParseFromString(start.payload), "expected the start request");
        Require(request.ip_address() == "127.0.0.1" && request.port() == port, "the start request names the wrong address");
        Send(WirelessMessageId::InfoRequest);
        const auto info = Read();
        aaw::WifiInfoResponse response;
        Require(info.id == static_cast<std::uint16_t>(WirelessMessageId::InfoResponse) && response.ParseFromString(info.payload), "expected the Wi-Fi details");
        Require(response.ssid() == "HEATUNIT-AA" && static_cast<int>(response.security_mode()) == 8, "the Wi-Fi details are wrong");
    }
private:
    int m_fd;
    WirelessFrameParser m_parser;
    std::deque<WirelessMessage> m_pending;
};

std::uint16_t PortOf(int listenFd)
{
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    Require(::getsockname(listenFd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "getsockname failed");
    return ntohs(address.sin_port);
}

int ConnectLocal(std::uint16_t port)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (fd >= 0 && ::connect(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0) return fd;
    if (fd >= 0) ::close(fd);
    return -1;
}

struct LinkRun {
    WirelessLink link;
    std::string phoneProblem;
    std::chrono::milliseconds duration{};
};

// The head unit's side against a fake phone that runs `phone` on its own thread.
LinkRun RunLink(const std::function<void(FakePhone&, int btFd, std::uint16_t port)>& phone)
{
    static Logger logger(std::filesystem::temp_directory_path() / "headunit-wireless-tests.log");
    int bluetooth[2];
    Require(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, bluetooth) == 0, "socketpair failed");
    std::string error;
    const int listener = ListenTcp(0, error);
    Require(listener >= 0, "ListenTcp failed");
    const auto port = PortOf(listener);
    LinkRun run;
    std::thread phoneThread([&] {
        try { FakePhone fake(bluetooth[1]); phone(fake, bluetooth[1], port); }
        catch (const std::exception& problem) { run.phoneProblem = problem.what(); }
    });
    const std::atomic_bool isStopRequested{false};
    const auto begin = std::chrono::steady_clock::now();
    run.link = EstablishWirelessLink(bluetooth[0], listener, {"HEATUNIT-AA", "secret-password", "dc:a6:32:01:02:03", "127.0.0.1", port},
        logger, isStopRequested, std::chrono::seconds(5));
    run.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    ::shutdown(bluetooth[0], SHUT_RDWR);   // releases a fake phone that still waits
    phoneThread.join();
    if (run.link.tcpFd >= 0) ::close(run.link.tcpFd);
    ::close(bluetooth[0]);
    ::close(bluetooth[1]);
    ::close(listener);
    return run;
}

void TestLinkConnects()
{
    int phoneTcp = -1;
    const auto run = RunLink([&](FakePhone& phone, int btFd, std::uint16_t port) {
        phone.AskForNetwork(port);
        aaw::WifiStartResponse started;
        started.set_status(aaw::STATUS_SUCCESS);
        phone.Send(WirelessMessageId::StartResponse, started.SerializeAsString());
        // Some phones hang up Bluetooth once they know the network; the TCP connection still follows.
        ::shutdown(btFd, SHUT_RDWR);
        phoneTcp = ConnectLocal(port);
        Require(phoneTcp >= 0, "the fake phone could not connect over TCP");
    });
    if (phoneTcp >= 0) ::close(phoneTcp);
    Require(run.phoneProblem.empty(), ("Fake phone: " + run.phoneProblem).c_str());
    Require(run.link.tcpFd >= 0 && run.link.hasSentInfo && run.link.peer == "127.0.0.1", ("The link was not established: " + run.link.message).c_str());
}

void TestLinkPhoneCannotJoin()
{
    const auto run = RunLink([](FakePhone& phone, int, std::uint16_t port) {
        phone.AskForNetwork(port);
        aaw::WifiConnectionStatus status;
        status.set_status(aaw::STATUS_WIFI_INCORRECT_CREDENTIALS);
        phone.Send(WirelessMessageId::ConnectionStatus, status.SerializeAsString());
    });
    Require(run.phoneProblem.empty(), ("Fake phone: " + run.phoneProblem).c_str());
    Require(run.link.tcpFd < 0, "A phone that cannot join must not count as connected");
    Require(run.link.message.find("-3") != std::string::npos, ("The failure must name the phone's status: " + run.link.message).c_str());
    Require(run.duration < std::chrono::seconds(3), "A reported failure must end the wait at once");
}

void TestLinkPhoneHangsUpEarly()
{
    const auto run = RunLink([](FakePhone& phone, int btFd, std::uint16_t) {
        phone.Read();   // the start request
        ::shutdown(btFd, SHUT_RDWR);
    });
    Require(run.phoneProblem.empty(), ("Fake phone: " + run.phoneProblem).c_str());
    Require(run.link.tcpFd < 0 && !run.link.hasSentInfo && !run.link.message.empty(), "A phone that hangs up before asking for the network is a failure");
    Require(run.duration < std::chrono::seconds(3), "A hang-up before the Wi-Fi details must end the wait at once");
}
}

void RunWirelessTests()
{
    TestFraming();
    TestHandshake();
    TestSocketTransport();
    TestStopWakesReader();
    TestLinkConnects();
    TestLinkPhoneCannotJoin();
    TestLinkPhoneHangsUpEarly();
}
