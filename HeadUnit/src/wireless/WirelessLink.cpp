#include "wireless/WirelessLink.h"
#include "wireless/SocketTransport.h"
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <vector>

namespace headunit {
using namespace std::chrono_literals;

WirelessLink EstablishWirelessLink(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested, std::chrono::milliseconds timeout)
{
    WirelessLink link;
    WirelessHandshake handshake(credentials);
    WirelessFrameParser parser;
    int rfcomm = rfcommFd;   // -1 once the phone has closed it
    const auto sendAll = [&](const std::vector<WirelessMessage>& messages) {
        for (const auto& message : messages) {
            const auto bytes = EncodeWirelessMessage(message);
            std::size_t offset = 0;
            while (offset < bytes.size()) {
                const auto sent = ::send(rfcomm, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
                if (sent < 0 && errno == EINTR) continue;
                if (sent <= 0) return false;
                offset += static_cast<std::size_t>(sent);
            }
        }
        return true;
    };
    const auto step = [&](const WirelessHandshakeStep& result) {
        if (!result.note.empty()) logger.Write("INFO", "WLAN", result.note);
        return sendAll(result.reply);
    };
    const auto started = std::chrono::steady_clock::now();
    const auto elapsed = [&] { return std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms"; };
    if (!step(handshake.Start())) { link.message = "Die Bluetooth-Verbindung zum Handy brach beim Senden ab."; return link; }

    const auto deadline = started + timeout;
    std::chrono::steady_clock::time_point infoSentAt{};
    while (!isStopRequested && std::chrono::steady_clock::now() < deadline) {
        pollfd waiting[2] = {{rfcomm, POLLIN, 0}, {listenFd, POLLIN, 0}};   // poll skips a negative descriptor
        const int ready = ::poll(waiting, 2, 200);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) { link.message = "Interner Fehler beim Warten auf das Handy."; return link; }
        if (waiting[1].revents & POLLIN) {
            link.tcpFd = AcceptTcp(listenFd, 0, link.peer);
            if (link.tcpFd >= 0) {
                link.hasSentInfo = handshake.HasSentInfo();
                logger.Write("INFO", "WLAN", "The phone opened the wireless connection from " + link.peer + " after " + elapsed());
                return link;
            }
        }
        if (rfcomm >= 0 && (waiting[0].revents & (POLLIN | POLLHUP | POLLERR))) {
            std::uint8_t buffer[1024];
            const auto got = ::recv(rfcomm, buffer, sizeof(buffer), 0);
            if (got > 0) {
                for (const auto& message : parser.Feed(buffer, static_cast<std::size_t>(got))) {
                    if (!step(handshake.OnMessage(message))) { link.message = "Die Bluetooth-Verbindung zum Handy brach beim Senden ab."; return link; }
                    if (handshake.IsFailed()) { link.message = handshake.Failure() + ". Ist das WLAN am Handy an?"; return link; }
                }
                if (handshake.HasSentInfo() && infoSentAt == std::chrono::steady_clock::time_point{}) {
                    infoSentAt = std::chrono::steady_clock::now();
                    logger.Write("INFO", "WLAN", "Wi-Fi details sent " + elapsed() + " after the start request; waiting for the phone to join");
                }
            } else if (got == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
                if (!handshake.HasSentInfo()) {
                    link.message = "Das Handy hat die Bluetooth-Verbindung beendet, bevor es nach dem WLAN gefragt hat.";
                    return link;
                }
                // Some phones hang up the Bluetooth socket once they know the network; the TCP connection still follows.
                logger.Write("INFO", "WLAN", "The phone closed the Bluetooth socket after receiving the Wi-Fi details; waiting for its TCP connection");
                rfcomm = -1;
            }
        }
        if (listenFd < 0 && infoSentAt != std::chrono::steady_clock::time_point{} && std::chrono::steady_clock::now() > infoSentAt + 5s) {
            link.hasSentInfo = true;
            link.message = "Die WLAN-Daten sind beim Handy angekommen.";
            return link;
        }
    }
    link.hasSentInfo = handshake.HasSentInfo();
    if (isStopRequested) link.message = "Abgebrochen.";
    else if (!link.hasSentInfo) link.message = "Zeitueberschreitung: Das Handy hat nicht nach dem WLAN gefragt.";
    else link.message = "Zeitueberschreitung: Das Handy hat die WLAN-Daten bekommen, ist dem WLAN aber nicht beigetreten "
        "(oder hat keine Verbindung zu Port " + std::to_string(credentials.port) + " aufgebaut).";
    return link;
}
}
