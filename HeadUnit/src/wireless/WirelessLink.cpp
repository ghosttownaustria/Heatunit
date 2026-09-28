#include "wireless/WirelessLink.h"
#include "wireless/TcpListener.h"
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <utility>
#include <vector>

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr int kPollMs = 200;
// Without a listening socket (the Bluetooth test), how long after the Wi-Fi details the link counts as done.
constexpr auto kInfoGrace = 5s;
constexpr const char* kSendFailed = "Die Bluetooth-Verbindung zum Handy brach beim Senden ab.";
}

// A setup over the phone's RFCOMM socket and the listening socket (-1 for none), for the network in `credentials`.
WirelessLinkSetup::WirelessLinkSetup(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested)
    : m_rfcommFd(rfcommFd), m_listenFd(listenFd), m_port(credentials.port), m_handshake(credentials), m_logger(logger), m_isStopRequested(isStopRequested), m_started(Clock::now())
{
}

// Starts the conversation and waits up to `timeout` for the phone: its messages on Bluetooth, and its TCP connection.
WirelessLink WirelessLinkSetup::Run(std::chrono::milliseconds timeout)
{
    if (!Step(m_handshake.Start())) return Failed(kSendFailed);
    const auto deadline = m_started + timeout;
    while (!m_isStopRequested && Clock::now() < deadline) {
        pollfd waiting[2] = {{m_rfcommFd, POLLIN, 0}, {m_listenFd, POLLIN, 0}};   // poll skips a negative descriptor
        const int ready = ::poll(waiting, 2, kPollMs);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return Failed("Interner Fehler beim Warten auf das Handy.");
        if ((waiting[1].revents & POLLIN) && AcceptPhone()) return m_link;
        if (m_rfcommFd >= 0 && (waiting[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            if (auto ended = ReadFromPhone()) return std::move(*ended);
        }
        if (m_listenFd < 0 && m_infoSentAt && Clock::now() > *m_infoSentAt + kInfoGrace) {
            m_link.hasSentInfo = true;
            m_link.message = "Die WLAN-Daten sind beim Handy angekommen.";
            return m_link;
        }
    }
    return TimedOut();
}

// Logs a step of the conversation and sends its reply; false when the phone's socket failed.
bool WirelessLinkSetup::Step(const WirelessHandshakeStep& step)
{
    if (!step.note.empty()) m_logger.Write(LogLevel::Info, "WLAN", step.note);
    return Send(step.reply);
}

// Sends the messages over the phone's Bluetooth socket; false when the socket failed.
bool WirelessLinkSetup::Send(const std::vector<WirelessMessage>& messages)
{
    for (const auto& message : messages) {
        const auto bytes = EncodeWirelessMessage(message);
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const auto sent = ::send(m_rfcommFd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
            if (sent < 0 && errno == EINTR) continue;
            if (sent <= 0) return false;
            offset += static_cast<std::size_t>(sent);
        }
    }
    return true;
}

// The phone's TCP connection arrived: the link is up.
bool WirelessLinkSetup::AcceptPhone()
{
    m_link.tcpFd = AcceptTcp(m_listenFd, 0, m_link.peer);
    if (m_link.tcpFd < 0) return false;
    m_link.hasSentInfo = m_handshake.HasSentInfo();
    m_logger.Write(LogLevel::Info, "WLAN", "The phone opened the wireless connection from " + m_link.peer + " after " + Elapsed());
    return true;
}

// Reads and answers what the phone said on Bluetooth. Returns the link when that ended it (a failure the phone reported,
// or a phone that hung up before it had the Wi-Fi details); nothing when the wait goes on. Some phones hang up the
// Bluetooth socket once they know the network; their TCP connection still follows.
std::optional<WirelessLink> WirelessLinkSetup::ReadFromPhone()
{
    std::uint8_t buffer[1024];
    const auto got = ::recv(m_rfcommFd, buffer, sizeof(buffer), 0);
    if (got > 0) {
        for (const auto& message : m_parser.Feed(buffer, static_cast<std::size_t>(got))) {
            if (!Step(m_handshake.OnMessage(message))) return Failed(kSendFailed);
            if (!m_handshake.IsFailed()) continue;
            m_link.hasSentInfo = m_handshake.HasSentInfo();   // then the phone had the details and could not join
            m_link.message = m_handshake.Failure() + ". " + WifiFailureAdvice(m_handshake.FailureStatus());
            return m_link;
        }
        if (m_handshake.HasSentInfo() && !m_infoSentAt) {
            m_infoSentAt = Clock::now();
            m_logger.Write(LogLevel::Info, "WLAN", "Wi-Fi details sent " + Elapsed() + " after the start request; waiting for the phone to join");
        }
        return std::nullopt;
    }
    if (got < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) return std::nullopt;
    if (!m_handshake.HasSentInfo()) return Failed("Das Handy hat die Bluetooth-Verbindung beendet, bevor es nach dem WLAN gefragt hat.");
    m_logger.Write(LogLevel::Info, "WLAN", "The phone closed the Bluetooth socket after receiving the Wi-Fi details; waiting for its TCP connection");
    m_rfcommFd = -1;
    return std::nullopt;
}

// The link failed with `message`.
WirelessLink WirelessLinkSetup::Failed(std::string message)
{
    m_link.message = std::move(message);
    return m_link;
}

// The wait ended without a connection: stopped, or the phone did not get as far as it should have.
WirelessLink WirelessLinkSetup::TimedOut()
{
    m_link.hasSentInfo = m_handshake.HasSentInfo();
    if (m_isStopRequested) m_link.message = "Abgebrochen.";
    else if (!m_link.hasSentInfo) m_link.message = "Zeitueberschreitung: Das Handy hat nicht nach dem WLAN gefragt.";
    else m_link.message = "Zeitueberschreitung: Das Handy hat die WLAN-Daten bekommen, ist dem WLAN aber nicht beigetreten "
        "(oder hat keine Verbindung zu Port " + std::to_string(m_port) + " aufgebaut).";
    return m_link;
}

// "1234 ms": the time since the conversation started.
std::string WirelessLinkSetup::Elapsed() const
{
    return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_started).count()) + " ms";
}

// Sets up the wireless link (see WirelessLinkSetup).
WirelessLink EstablishWirelessLink(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested, std::chrono::milliseconds timeout)
{
    return WirelessLinkSetup(rfcommFd, listenFd, credentials, logger, isStopRequested).Run(timeout);
}
}
