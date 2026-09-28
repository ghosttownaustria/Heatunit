#pragma once
#include "logging/Logger.h"
#include "wireless/WirelessFrameParser.h"
#include "wireless/WirelessHandshake.h"
#include "wireless/WirelessProtocol.h"
#include <atomic>
#include <chrono>
#include <optional>
#include <string>

namespace headunit {
// The outcome of setting up the wireless link.
struct WirelessLink {
    int tcpFd{-1};           // the phone's TCP connection (the caller owns it), or -1
    bool hasSentInfo{};      // the phone asked for the Wi-Fi details and got them
    std::string peer;        // the phone's address in the Wi-Fi network
    std::string message;     // why there is no connection (German, for the window)
};

// The conversation with the phone over its Bluetooth (RFCOMM) socket, and then the wait for the phone's TCP connection
// on the listening socket. Success is that TCP connection. Without a listening socket (-1, the Bluetooth test) it ends a
// few seconds after the Wi-Fi details went out. Neither socket is closed here. No Qt: tested against a simulated phone.
class WirelessLinkSetup {
public:
    WirelessLinkSetup(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger, const std::atomic_bool& isStopRequested);

    WirelessLink Run(std::chrono::milliseconds timeout);

private:
    using Clock = std::chrono::steady_clock;

    int m_rfcommFd;   // -1 once the phone has closed it
    int m_listenFd;
    std::uint16_t m_port;
    WirelessHandshake m_handshake;
    WirelessFrameParser m_parser;
    Logger& m_logger;
    const std::atomic_bool& m_isStopRequested;
    Clock::time_point m_started;
    std::optional<Clock::time_point> m_infoSentAt;
    WirelessLink m_link;

    bool Step(const WirelessHandshakeStep& step);
    bool Send(const std::vector<WirelessMessage>& messages);
    bool AcceptPhone();
    std::optional<WirelessLink> ReadFromPhone();
    WirelessLink Failed(std::string message);
    WirelessLink TimedOut();
    std::string Elapsed() const;
};

WirelessLink EstablishWirelessLink(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested, std::chrono::milliseconds timeout);
}
