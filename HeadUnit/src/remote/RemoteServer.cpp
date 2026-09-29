#include "remote/RemoteServer.h"
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>
#include <utility>

namespace headunit {
namespace {
constexpr int kMaxLineBytes = 4096;
}

RemoteServer::RemoteServer(RemoteCommandDeps deps, Logger& logger, QObject* parent)
    : QObject(parent), m_command(std::move(deps)), m_logger(logger), m_server(new QTcpServer(this))
{
    connect(m_server, &QTcpServer::newConnection, this, [this] { AcceptConnections(); });
}

// Starts listening on 127.0.0.1 only. A port that is taken is not fatal: the program runs on without the remote API.
bool RemoteServer::Listen(int port)
{
    if (!m_server->listen(QHostAddress::LocalHost, static_cast<quint16>(port))) {
        m_logger.Write(LogLevel::Warning, "API", "Remote API not available on port " + std::to_string(port) + ": " + m_server->errorString().toStdString());
        return false;
    }
    m_logger.Write(LogLevel::Info, "API", "Remote API listens on 127.0.0.1:" + std::to_string(m_server->serverPort()));
    return true;
}

// Takes over the waiting connections; each is answered line by line until it closes.
void RemoteServer::AcceptConnections()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { AnswerLines(*socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

// Answers every complete line that has arrived. A line that is too long closes the connection; a bad line only gets an
// error reply.
void RemoteServer::AnswerLines(QTcpSocket& socket)
{
    while (socket.canReadLine()) {
        const QByteArray line = socket.readLine(kMaxLineBytes + 1);
        if (!line.endsWith('\n')) {
            socket.disconnectFromHost();
            return;
        }
        const QByteArray text = line.trimmed();
        if (text.isEmpty()) continue;
        socket.write(QByteArray::fromStdString(m_command.Execute(text.toStdString())) + '\n');
    }
    if (socket.bytesAvailable() > kMaxLineBytes) socket.disconnectFromHost();
}
}
