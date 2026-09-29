#pragma once
#include "logging/Logger.h"
#include "remote/RemoteCommand.h"
#include <QObject>

class QTcpServer;
class QTcpSocket;

namespace headunit {
// Listens on the loopback address for the commands of other programs (docs/api.md) and answers each line through
// RemoteCommand. Lives in the GUI thread, so the commands run there like the simulated console's own actions.
class RemoteServer final : public QObject {
public:
    RemoteServer(RemoteCommandDeps deps, Logger& logger, QObject* parent);

    bool Listen(int port);

private:
    RemoteCommand m_command;
    Logger& m_logger;
    QTcpServer* m_server{};

    void AcceptConnections();
    void AnswerLines(QTcpSocket& socket);
};
}
