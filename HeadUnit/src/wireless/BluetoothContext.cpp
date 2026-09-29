#include "wireless/BluetoothContext.h"
#include "wireless/BluezCalls.h"
#include <QDBusConnection>
#include <unistd.h>
#include <utility>

namespace headunit {
namespace {
// How long a manual phone may connect after the person pressed Connect for it.
constexpr auto kManualConnectionWindow = std::chrono::minutes(2);
}

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

// The phones that connect only when the person asks (any thread).
void BluetoothContext::SetManualPhones(std::set<QString> devicePaths)
{
    std::lock_guard lock(m_mutex);
    m_manualPhones = std::move(devicePaths);
}

// Whether the phone connects only when the person asks (any thread).
bool BluetoothContext::IsManual(const QString& devicePath)
{
    std::lock_guard lock(m_mutex);
    return m_manualPhones.contains(devicePath);
}

// The person asked for this phone: it may open the Android Auto service for a while, also when it is a manual phone.
void BluetoothContext::AllowManualConnection(const QString& devicePath)
{
    std::lock_guard lock(m_mutex);
    m_allowedUntil[devicePath] = std::chrono::steady_clock::now() + kManualConnectionWindow;
}

// A phone opened the Android Auto service: its connected socket waits for WaitForPhone. A manual phone that nobody asked
// for is turned away, so that it does not start Android Auto by itself.
void BluetoothContext::AddPhone(int fd, const QString& devicePath)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_manualPhones.contains(devicePath)) {
            const auto allowed = m_allowedUntil.find(devicePath);
            const bool isAsked = allowed != m_allowedUntil.end() && std::chrono::steady_clock::now() < allowed->second;
            if (!isAsked) {
                ::close(fd);
                Log(LogLevel::Info, "Phone " + bluez::Text(devicePath) + " is set to connect manually; its Android Auto connection was turned away");
                return;
            }
            m_allowedUntil.erase(allowed);
        }
        m_phones.push_back(fd);
        m_phonePaths[fd] = devicePath;
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
    for (const int fd : m_phones) {
        ::close(fd);
        m_phonePaths.erase(fd);
    }
    m_phones.clear();
}

// The device whose Android Auto socket `fd` is (empty for an unknown socket).
QString BluetoothContext::PhonePathOf(int fd)
{
    std::lock_guard lock(m_mutex);
    const auto found = m_phonePaths.find(fd);
    return found != m_phonePaths.end() ? found->second : QString();
}

// Notes the phone a wireless session runs on (empty: none runs).
void BluetoothContext::SetAndroidAutoPhone(const QString& devicePath)
{
    std::lock_guard lock(m_mutex);
    m_androidAutoPhone = devicePath;
}

// Tells the window which phones are paired, which are connected and which one runs Android Auto (service thread; it
// asks BlueZ).
void BluetoothContext::PublishPhones()
{
    if (!m_events.onPhonesChanged) return;
    QString androidAutoPhone;
    {
        std::lock_guard lock(m_mutex);
        androidAutoPhone = m_androidAutoPhone;
    }
    auto bus = QDBusConnection::systemBus();
    std::vector<BluetoothPhone> phones;
    for (const auto& device : bluez::ReadBluez(bus).paired) {
        if (device.isPhone) phones.push_back({bluez::Text(device.path), device.name, device.isConnected, device.path == androidAutoPhone});
    }
    m_events.onPhonesChanged(phones);
}

// Tells the window that no phones can be listed (the service stopped).
void BluetoothContext::PublishNoPhones() const
{
    if (m_events.onPhonesChanged) m_events.onPhonesChanged({});
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
