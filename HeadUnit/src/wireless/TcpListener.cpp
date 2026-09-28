#include "wireless/TcpListener.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace headunit {
// A TCP socket listening on all addresses (close-on-exec); -1 with `error` filled when that fails. The caller closes it.
int ListenTcp(std::uint16_t port, std::string& error)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        error = std::string("Socket: ") + std::strerror(errno);
        return -1;
    }
    const int isOn = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &isOn, sizeof(isOn));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 || ::listen(fd, 1) != 0) {
        error = "Port " + std::to_string(port) + " laesst sich nicht oeffnen: " + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    return fd;
}

// Waits up to `timeoutMs` for a connection on `listenFd`; the accepted socket (TCP_NODELAY, close-on-exec) or -1. `peer`
// receives the phone's address.
int AcceptTcp(int listenFd, int timeoutMs, std::string& peer)
{
    pollfd waiting{listenFd, POLLIN, 0};
    if (::poll(&waiting, 1, timeoutMs) <= 0) return -1;
    sockaddr_in address{};
    socklen_t length = sizeof(address);
    const int fd = ::accept4(listenFd, reinterpret_cast<sockaddr*>(&address), &length, SOCK_CLOEXEC);
    if (fd < 0) return -1;
    const int isOn = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &isOn, sizeof(isOn));
    char text[INET_ADDRSTRLEN]{};
    peer = inet_ntop(AF_INET, &address.sin_addr, text, sizeof(text)) ? text : "?";
    return fd;
}
}
