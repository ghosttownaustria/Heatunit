#include "wireless/BluetoothContext.h"
#include "wireless/BluezCalls.h"
#include <QDBusConnection>
#include <unistd.h>
#include <utility>

namespace headunit {
// A context that logs to `logger` and tells the window through `events`.
BluetoothContext::BluetoothContext(Logger& logger, BluetoothEvents events) : m_logger(logger), m_events(std::move(events))
{
}

// A line of the Bluetooth log.
void BluetoothContext::Log(LogLevel level, const std::string& message) const
{
    m_logger.Write(level, "BT", message);
}

// A step for the person at the head unit.
void BluetoothContext::Tell(const std::string& text) const
{
    if (m_events.onStatus) m_events.onStatus(text);
}

// Whether the head unit has a screen that asks the person (otherwise every pairing is confirmed at once).
bool BluetoothContext::CanAskForPairing() const
{
    return static_cast<bool>(m_events.onPairingRequest);
}

// BlueZ asks to confirm a pairing: its call is answered later, from the person's answer on the head unit's screen (a
// delayed D-Bus reply, see MakeAnswer). An older question that was never answered is refused first.
void BluetoothContext::AskForPairing(const QDBusMessage& message, const QString& devicePath, const std::string& phone, const std::string& code)
{
    EndPairing(true);
    message.setDelayedReply(true);
    std::uint64_t id = 0;
    {
        std::lock_guard lock(m_pairing->mutex);
        id = ++m_pairing->id;
        m_pairing->request = message;
        m_pairing->devicePath = devicePath;
    }
    Tell("Bluetooth-Kopplung mit " + phone + ": Code " + code + " am Handy und hier bestaetigen.");
    m_events.onPairingRequest({phone, code, MakeAnswer(id)});
}

// Drops a waiting pairing request, refusing it when BlueZ still waits for the answer, and takes the question away.
void BluetoothContext::EndPairing(bool isRefusing)
{
    bool wasPending = false;
    {
        std::lock_guard lock(m_pairing->mutex);
        if (m_pairing->request && isRefusing)
            QDBusConnection::systemBus().send(m_pairing->request->createErrorReply(QStringLiteral("org.bluez.Error.Canceled"), QStringLiteral("Pairing ended")));
        wasPending = m_pairing->request.has_value();
        m_pairing->request.reset();
        ++m_pairing->id;
    }
    if (wasPending && m_events.onPairingEnd) m_events.onPairingEnd();
}

// A phone opened the Android Auto service: its connected socket waits for WaitForPhone.
void BluetoothContext::AddPhone(int fd)
{
    {
        std::lock_guard lock(m_mutex);
        m_phones.push_back(fd);
    }
    m_hasPhone.notify_all();
}

// Waits up to `timeout` for a phone that opened the Android Auto service; its socket (the caller owns it), or -1.
int BluetoothContext::WaitForPhone(std::chrono::milliseconds timeout)
{
    std::unique_lock lock(m_mutex);
    if (!m_hasPhone.wait_for(lock, timeout, [this] { return !m_phones.empty(); })) return -1;
    const int fd = m_phones.front();
    m_phones.pop_front();
    return fd;
}

// Closes the sockets of phones nobody took.
void BluetoothContext::CloseWaitingPhones()
{
    std::lock_guard lock(m_mutex);
    for (const int fd : m_phones) ::close(fd);
    m_phones.clear();
}

// Notes a phone that was connected before the service existed (service thread).
void BluetoothContext::RememberConnectedBefore(const QString& devicePath)
{
    m_connectedBefore.insert(devicePath);
}

// A phone that connects from now on finds the Android Auto service; it needs no reconnecting (service thread).
void BluetoothContext::ForgetConnectedBefore(const QString& devicePath)
{
    m_connectedBefore.erase(devicePath);
}

// Whether the phone was connected before the service existed (service thread).
bool BluetoothContext::WasConnectedBefore(const QString& devicePath) const
{
    return m_connectedBefore.contains(devicePath);
}

// Forgets every phone connected before (service thread).
void BluetoothContext::ClearConnectedBefore()
{
    m_connectedBefore.clear();
}

// The answer to pairing request `id`, for the head unit's question. It replies to BlueZ's waiting call from whatever
// thread it runs on (QDBusConnection::send is thread-safe); the logger lives as long as the program.
std::function<void(bool)> BluetoothContext::MakeAnswer(std::uint64_t id) const
{
    return [weak = std::weak_ptr<PendingPairing>(m_pairing), id, logger = &m_logger](bool isAccepted) {
        const auto pairing = weak.lock();
        if (!pairing) return;
        std::lock_guard lock(pairing->mutex);
        if (pairing->id != id || !pairing->request) return;
        const auto& request = *pairing->request;
        QDBusConnection::systemBus().send(isAccepted ? request.createReply()
                                                     : request.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Refused at the head unit")));
        if (isAccepted) bluez::Trust(pairing->devicePath);
        logger->Write(LogLevel::Info, "BT", std::string("Pairing ") + (isAccepted ? "confirmed" : "refused") + " at the head unit");
        pairing->request.reset();
    };
}
}
