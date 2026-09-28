#pragma once
#include <cstdint>
#include <string>

namespace headunit {
int ListenTcp(std::uint16_t port, std::string& error);
int AcceptTcp(int listenFd, int timeoutMs, std::string& peer);
}
