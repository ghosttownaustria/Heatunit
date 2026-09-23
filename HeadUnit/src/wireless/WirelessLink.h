#pragma once
#include "logging/Logger.h"
#include "wireless/WirelessProtocol.h"
#include <atomic>
#include <chrono>
#include <string>

namespace headunit {
struct WirelessLink {
    int tcpFd{-1};           // the phone's TCP connection (the caller owns it), or -1
    bool hasSentInfo{};      // the phone asked for the Wi-Fi details and got them
    std::string peer;        // the phone's address in the Wi-Fi network
    std::string message;     // why there is no connection (German, for the window)
};

// The conversation with the phone over its Bluetooth (RFCOMM) socket, and then the wait for the phone's TCP connection
// on `listenFd`. Success is that TCP connection. Without a listening socket (-1, the Bluetooth test) it ends a few
// seconds after the Wi-Fi details went out. Neither socket is closed here. No Qt: tested against a simulated phone.
WirelessLink EstablishWirelessLink(int rfcommFd, int listenFd, const WifiCredentials& credentials, Logger& logger,
    const std::atomic_bool& isStopRequested, std::chrono::milliseconds timeout);
}
