// Wireless Android Auto without a phone: the message framing and conversation of the Bluetooth link, and the
// socket transport of the Wi-Fi link (over a socketpair). Linux only, like the code it tests.
#include "ProtocolTestSuites.h"
#include "TestSupport.h"
#include "wireless/SocketTransport.h"
#include "wireless/TcpListener.h"
#include "wireless/WirelessFrameParser.h"
#include "wireless/WirelessHandshake.h"
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
// The network the tests announce.
WifiCredentials Credentials() { return {"HEATUNIT-AA", "secret-password", "DC:A6:32:01:02:03", "10.42.0.1", 5288}; }

// The bytes of `text`.
std::vector<std::uint8_t> Bytes(const std::string& text) { return {text.begin(), text.end()}; }

// The framing of the Bluetooth messages: size, id and payload, and a parser that waits for complete messages.
void TestFraming()
{
    const auto bytes = EncodeWirelessMessage({7, "abc"});
    Check(bytes == std::vector<std::uint8_t>({0, 3, 0, 7, 'a', 'b', 'c'}), "A wireless message is size (2 bytes), id (2 bytes), payload, big endian");
    const auto empty = EncodeWirelessMessage({2, ""});
    Check(empty == std::vector<std::uint8_t>({0, 0, 0, 2}), "An empty message is just its header");
    const auto large = EncodeWirelessMessage({1, std::string(300, 'x')});
    Check(large.size() == 304 && large[0] == 1 && large[1] == 44, "The size of a large payload is written big endian");

    // Two messages, delivered one byte at a time: the parser waits for each to be complete.
    auto stream = EncodeWirelessMessage({2, ""});
    const auto second = EncodeWirelessMessage({7, "hello"});
    stream.insert(stream.end(), second.begin(), second.end());
    WirelessFrameParser parser;
    std::vector<WirelessMessage> messages;
    for (const auto byte : stream)
        for (auto& message : parser.Feed(&byte, 1)) messages.push_back(std::move(message));
    Check(messages.size() == 2, "Byte-wise input produced the wrong number of messages");
    Check(messages[0].id == 2 && messages[0].payload.empty() && messages[1].id == 7 && messages[1].payload == "hello", "Byte-wise input changed a message");

    // The same two messages in one piece, plus the start of a third.
    WirelessFrameParser whole;
    auto burst = stream;
    burst.push_back(0);
    burst.push_back(5);
    const auto parsed = whole.Feed(burst.data(), burst.size());
    Check(parsed.size() == 2, "A partial third message must not be delivered");
    const auto rest = Bytes(std::string("\x00\x06" "abcde", 7));
    Check(whole.Feed(rest.data(), rest.size()).size() == 1, "The completed third message was not delivered");
}

// The first message the head unit answers with, if any.
const WirelessMessage* First(const WirelessHandshakeStep& step) { return step.reply.empty() ? nullptr : &step.reply.front(); }

// The conversation over Bluetooth: start request, the Wi-Fi details on request, and the phone's answers.
void TestHandshake()
{
    WirelessHandshake handshake(Credentials());
    const auto start = handshake.Start();
    const auto* startMessage = First(start);
    Check(startMessage && start.reply.size() == 1 && startMessage->id == static_cast<std::uint16_t>(WirelessMessageId::StartRequest), "The conversation starts with the start request");
    aaw::WifiStartRequest request;
    Check(request.ParseFromString(startMessage->payload) && request.ip_address() == "10.42.0.1" && request.port() == 5288, "The start request names the head unit's address and port");

    const auto info = handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::InfoRequest), ""});
    const auto* infoMessage = First(info);
    Check(infoMessage && infoMessage->id == static_cast<std::uint16_t>(WirelessMessageId::InfoResponse), "The Wi-Fi details answer the info request");
    aaw::WifiInfoResponse response;
    Check(response.ParseFromString(infoMessage->payload), "The info response could not be read back");
    Check(response.ssid() == "HEATUNIT-AA" && response.password() == "secret-password" && response.bssid() == "DC:A6:32:01:02:03", "The info response carries the network");
    // Android's numbering: WPA2 personal is 8. The imported enum's WPA2_PERSONAL (5) is unknown to the phone.
    Check(static_cast<int>(response.security_mode()) == kWifiSecurityWpa2Personal && kWifiSecurityWpa2Personal == 8, "The network must be announced as WPA2 in Android's numbering (8)");
    Check(infoMessage->payload.find(std::string("\x20\x08", 2)) != std::string::npos, "Field 4 (security mode) must be the varint 8 on the wire");
    Check(handshake.HasSentInfo() && !handshake.IsFailed(), "State after the info response");

    aaw::WifiStartResponse good;
    good.set_status(aaw::STATUS_SUCCESS);
    const auto accepted = handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), good.SerializeAsString()});
    Check(accepted.reply.empty() && !handshake.IsFailed(), "A successful start response ends the conversation quietly");

    aaw::WifiConnectionStatus bad;
    bad.set_status(aaw::STATUS_WIFI_INCORRECT_CREDENTIALS);
    handshake.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::ConnectionStatus), bad.SerializeAsString()});
    Check(handshake.IsFailed() && !handshake.Failure().empty(), "A negative connection status is a failure");

    WirelessHandshake other(Credentials());
    aaw::WifiStartResponse refused;
    refused.set_status(aaw::STATUS_PHONE_WIFI_DISABLED);
    other.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), refused.SerializeAsString()});
    Check(other.IsFailed(), "A negative start response is a failure");

    WirelessHandshake quiet(Credentials());
    const auto ignored = quiet.OnMessage({99, "whatever"});
    Check(ignored.reply.empty() && !ignored.note.empty() && !quiet.IsFailed(), "An unknown message is logged and ignored");
    const auto garbage = quiet.OnMessage({static_cast<std::uint16_t>(WirelessMessageId::StartResponse), "\xff\xff\xff"});
    Check(garbage.reply.empty() && !quiet.IsFailed(), "An unreadable message does not end the conversation");
}

using aasdk::common::Data;
using aasdk::error::Error;
using aasdk::error::ErrorCode;

// How a transport request ended.
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

// Reads `size` bytes from the transport.
Outcome Receive(const std::shared_ptr<SocketTransport>& transport, std::size_t size)
{
    return Await([&](boost::asio::io_context& io, auto resolve, auto reject) {
        auto promise = aasdk::transport::ITransport::ReceivePromise::defer(io);
        promise->then(resolve, reject);
        transport->receive(size, promise);
    });
}

// Writes `data` to the transport.
Outcome Send(const std::shared_ptr<SocketTransport>& transport, Data data)
{
    return Await([&](boost::asio::io_context& io, auto resolve, auto reject) {
        auto promise = aasdk::transport::ITransport::SendPromise::defer(io);
        promise->then([resolve] { resolve(Data{}); }, reject);
        transport->send(std::move(data), promise);
    });
}

// The TCP transport collects partial reads, keeps the surplus, reports a closed peer and aborts everything after stop().
void TestSocketTransport()
{
    int ends[2];
    Check(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0, "socketpair failed");
    auto transport = std::make_shared<SocketTransport>(ends[0]);

    // Bytes arriving in pieces are collected until the requested size is complete, and the surplus is kept.
    Check(::write(ends[1], "ab", 2) == 2, "write failed");
    std::thread late([&] { std::this_thread::sleep_for(std::chrono::milliseconds(200)); Check(::write(ends[1], "cdef", 4) == 4, "write failed"); });
    const auto first = Receive(transport, 4);
    late.join();
    Check(first.isResolved && first.data == Bytes("abcd"), "A read that needs two arrivals returned the wrong bytes");
    const auto second = Receive(transport, 2);
    Check(second.isResolved && second.data == Bytes("ef"), "The surplus of the previous read was lost");

    // What is sent arrives in order.
    const auto sent = Send(transport, Bytes("hello"));
    Check(sent.isResolved, "Sending was not acknowledged");
    char received[8]{};
    Check(::read(ends[1], received, sizeof(received)) == 5 && std::string(received, 5) == "hello", "The peer did not get what was sent");

    // A peer that goes away is an error, not a hang.
    ::close(ends[1]);
    const auto closed = Receive(transport, 4);
    Check(closed.isRejected && closed.code == ErrorCode::TCP_TRANSFER, "A closed connection must reject the read with TCP_TRANSFER");

    // After stop() every request is rejected with OPERATION_ABORTED, and stop() may be repeated.
    transport->stop();
    transport->stop();
    const auto stopped = Receive(transport, 4);
    Check(stopped.isRejected && stopped.code == ErrorCode::OPERATION_ABORTED, "A read after stop() must be aborted");
    const auto stoppedSend = Send(transport, Bytes("x"));
    Check(stoppedSend.isRejected && stoppedSend.code == ErrorCode::OPERATION_ABORTED, "A write after stop() must be aborted");
}

// stop() must wake a read that is waiting for a phone that says nothing.
void TestStopWakesReader()
{
    int ends[2];
    Check(::socketpair(AF_UNIX, SOCK_STREAM, 0, ends) == 0, "socketpair failed");
    auto transport = std::make_shared<SocketTransport>(ends[0]);
    std::thread stopper([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); transport->stop(); });
    const auto begin = std::chrono::steady_clock::now();
    const auto outcome = Receive(transport, 4);
    stopper.join();
    Check(outcome.isRejected && outcome.code == ErrorCode::OPERATION_ABORTED, "A waiting read must be aborted by stop()");
    Check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(3), "stop() took too long to reach a waiting read");
    ::close(ends[1]);
}

// A phone at the other end of the "Bluetooth" socket (a socketpair): reads what the head unit says, answers like a phone.
class FakePhone {
public:
    // A phone on `fd`, which the caller owns.
    explicit FakePhone(int fd) : m_fd(fd) {}
    // The next message of the head unit; throws when the head unit closed the socket.
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
    // Sends a message to the head unit.
    void Send(WirelessMessageId id, const std::string& payload = {})
    {
        const auto bytes = EncodeWirelessMessage({static_cast<std::uint16_t>(id), payload});
        Check(::send(m_fd, bytes.data(), bytes.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(bytes.size()), "the fake phone could not send");
    }
    // The usual start: the start request arrives, the phone asks for the network and gets it.
    void AskForNetwork(std::uint16_t port)
    {
        const auto start = Read();
        aaw::WifiStartRequest request;
        Check(start.id == static_cast<std::uint16_t>(WirelessMessageId::StartRequest) && request.ParseFromString(start.payload), "expected the start request");
        Check(request.ip_address() == "127.0.0.1" && request.port() == port, "the start request names the wrong address");
        Send(WirelessMessageId::InfoRequest);
        const auto info = Read();
        aaw::WifiInfoResponse response;
        Check(info.id == static_cast<std::uint16_t>(WirelessMessageId::InfoResponse) && response.ParseFromString(info.payload), "expected the Wi-Fi details");
        Check(response.ssid() == "HEATUNIT-AA" && static_cast<int>(response.security_mode()) == 8, "the Wi-Fi details are wrong");
    }
private:
    int m_fd;
    WirelessFrameParser m_parser;
    std::deque<WirelessMessage> m_pending;
};

// The port a listening socket got.
std::uint16_t PortOf(int listenFd)
{
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    Check(::getsockname(listenFd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "getsockname failed");
    return ntohs(address.sin_port);
}

// A TCP connection to `port` on this computer, or -1.
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

// How a link setup against the fake phone went.
struct LinkRun {
    WirelessLink link;
    std::string phoneProblem;
    std::chrono::milliseconds duration{};
};

// The head unit's side against a fake phone that runs `phone` on its own thread.
LinkRun RunLink(const std::function<void(FakePhone&, int btFd, std::uint16_t port)>& phone)
{
    int bluetooth[2];
    Check(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, bluetooth) == 0, "socketpair failed");
    std::string error;
    const int listener = ListenTcp(0, error);
    Check(listener >= 0, "ListenTcp failed");
    const auto port = PortOf(listener);
    LinkRun run;
    std::thread phoneThread([&] {
        try { FakePhone fake(bluetooth[1]); phone(fake, bluetooth[1], port); }
        catch (const std::exception& problem) { run.phoneProblem = problem.what(); }
    });
    const std::atomic_bool isStopRequested{false};
    const auto begin = std::chrono::steady_clock::now();
    run.link = EstablishWirelessLink(bluetooth[0], listener, {"HEATUNIT-AA", "secret-password", "dc:a6:32:01:02:03", "127.0.0.1", port},
        TestLogger(), isStopRequested, std::chrono::seconds(5));
    run.duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin);
    ::shutdown(bluetooth[0], SHUT_RDWR);   // releases a fake phone that still waits
    phoneThread.join();
    if (run.link.tcpFd >= 0) ::close(run.link.tcpFd);
    ::close(bluetooth[0]);
    ::close(bluetooth[1]);
    ::close(listener);
    return run;
}

// A phone that joins the network and connects over TCP establishes the link, even after hanging up Bluetooth.
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
        Check(phoneTcp >= 0, "the fake phone could not connect over TCP");
    });
    if (phoneTcp >= 0) ::close(phoneTcp);
    Check(run.phoneProblem.empty(), "Fake phone: " + run.phoneProblem);
    Check(run.link.tcpFd >= 0 && run.link.hasSentInfo && run.link.peer == "127.0.0.1", "The link was not established: " + run.link.message);
}

// A phone that reports it cannot join ends the wait at once, with its reason.
void TestLinkPhoneCannotJoin()
{
    const auto run = RunLink([](FakePhone& phone, int, std::uint16_t port) {
        phone.AskForNetwork(port);
        aaw::WifiConnectionStatus status;
        status.set_status(aaw::STATUS_WIFI_INCORRECT_CREDENTIALS);
        phone.Send(WirelessMessageId::ConnectionStatus, status.SerializeAsString());
    });
    Check(run.phoneProblem.empty(), "Fake phone: " + run.phoneProblem);
    Check(run.link.tcpFd < 0, "A phone that cannot join must not count as connected");
    // The station takes this as a phone that could not find the network (and then broadcasts a hidden one's name).
    Check(run.link.hasSentInfo, "A phone that got the Wi-Fi details and could not join must be reported as having them");
    Check(run.link.message.find("-3") != std::string::npos, "The failure must name the phone's status: " + run.link.message);
    Check(run.link.message.find("Passwort") != std::string::npos, "A wrong password must be named as such: " + run.link.message);
    Check(run.duration < std::chrono::seconds(3), "A reported failure must end the wait at once");
}

// A phone that hangs up before it asked for the network ends the wait at once.
void TestLinkPhoneHangsUpEarly()
{
    const auto run = RunLink([](FakePhone& phone, int btFd, std::uint16_t) {
        phone.Read();   // the start request
        ::shutdown(btFd, SHUT_RDWR);
    });
    Check(run.phoneProblem.empty(), "Fake phone: " + run.phoneProblem);
    Check(run.link.tcpFd < 0 && !run.link.hasSentInfo && !run.link.message.empty(), "A phone that hangs up before asking for the network is a failure");
    Check(run.duration < std::chrono::seconds(3), "A hang-up before the Wi-Fi details must end the wait at once");
}
}

// Wireless Android Auto without Qt and without a phone: framing, the conversation, the TCP transport and the link setup.
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
