#pragma once
#include "wireless/WirelessProtocol.h"
#include <string>
#include <vector>

namespace headunit {
// What one step of the conversation produced: the messages to send to the phone and a line for the log.
struct WirelessHandshakeStep {
    std::vector<WirelessMessage> reply;
    std::string note;
};

// The head unit's side of the Bluetooth conversation (see WirelessProtocol.h): it announces where to connect, answers
// the phone's question for the network, and notes when the phone reports that it cannot use it.
class WirelessHandshake {
public:
    explicit WirelessHandshake(WifiCredentials credentials);

    WirelessHandshakeStep Start();
    WirelessHandshakeStep OnMessage(const WirelessMessage& message);
    bool IsFailed() const;
    const std::string& Failure() const;
    int FailureStatus() const;
    bool HasSentInfo() const;

private:
    WifiCredentials m_credentials;
    bool m_isFailed{};
    bool m_hasSentInfo{};
    std::string m_failure;
    int m_failureStatus{};

    WirelessHandshakeStep AnswerInfoRequest();
    WirelessHandshakeStep NoteFailure(int status, std::string failure);
};

std::string WifiFailureAdvice(int status);
}
