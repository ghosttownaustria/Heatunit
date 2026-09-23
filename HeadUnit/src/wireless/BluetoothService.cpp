#include "wireless/BluetoothService.h"
#include "wireless/WirelessProtocol.h"
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QEventLoop>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <deque>
#include <fcntl.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace headunit {
namespace bluez {

// What the D-Bus objects share with the service. Everything runs on the thread that calls the service.
struct Shared {
    Logger* logger{};
    std::deque<int> phones;   // connected RFCOMM sockets of phones that opened the Android Auto service
    void Log(const std::string& level, const std::string& message) const { if (logger) logger->Write(level, "BT", message); }
};

std::string Text(const QString& value) { return value.toStdString(); }

// Pairing without a PIN ("Just Works"): every request from a phone that asks to pair is accepted. A car head unit
// has no way to type a PIN, and the phone shows its own confirmation.
class AgentAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    AgentAdaptor(QObject* parent, Shared* shared) : QDBusAbstractAdaptor(parent), m_shared(shared) {}
public slots:
    void Release() { m_shared->Log("INFO", "Pairing agent released by BlueZ"); }
    QString RequestPinCode(const QDBusObjectPath& device) { m_shared->Log("INFO", "PIN requested by " + Text(device.path()) + "; answering 0000"); return QStringLiteral("0000"); }
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pin) { m_shared->Log("INFO", "PIN for " + Text(device.path()) + ": " + Text(pin)); }
    quint32 RequestPasskey(const QDBusObjectPath& device) { m_shared->Log("INFO", "Passkey requested by " + Text(device.path()) + "; answering 0"); return 0; }
    void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16) { m_shared->Log("INFO", "Passkey for " + Text(device.path()) + ": " + std::to_string(passkey)); }
    void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey) { m_shared->Log("INFO", "Pairing with " + Text(device.path()) + " confirmed (code " + std::to_string(passkey) + ")"); }
    void RequestAuthorization(const QDBusObjectPath& device) { m_shared->Log("INFO", "Pairing authorized for " + Text(device.path())); }
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid) { m_shared->Log("INFO", "Service " + Text(uuid) + " authorized for " + Text(device.path())); }
    void Cancel() { m_shared->Log("INFO", "Pairing request cancelled"); }
private:
    Shared* m_shared;
};

// The Android Auto Wireless service. BlueZ hands over the connected socket of every phone that opens it.
class ProfileAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")
public:
    ProfileAdaptor(QObject* parent, Shared* shared) : QDBusAbstractAdaptor(parent), m_shared(shared) {}
public slots:
    void Release() { m_shared->Log("INFO", "Android Auto service released by BlueZ"); }
    void NewConnection(const QDBusObjectPath& device, const QDBusUnixFileDescriptor& fd, const QVariantMap&)
    {
        // The descriptor object closes its own copy when the call ends, so keep a duplicate.
        const int own = fd.isValid() ? ::fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 0) : -1;
        if (own < 0) { m_shared->Log("ERROR", "Connection from " + Text(device.path()) + " without a usable socket"); return; }
        m_shared->Log("INFO", "Phone " + Text(device.path()) + " opened the Android Auto Wireless service");
        m_shared->phones.push_back(own);
    }
    void RequestDisconnection(const QDBusObjectPath& device) { m_shared->Log("INFO", "Phone " + Text(device.path()) + " disconnected from the Android Auto service"); }
private:
    Shared* m_shared;
};

// Only carries the adaptors (D-Bus objects need an object to be registered under a path), and logs what the
// phones do to their Bluetooth link: whether a phone connects at all is the first thing to know when nothing happens.
class ExportedObject : public QObject {
    Q_OBJECT
public:
    explicit ExportedObject(Shared* shared) : m_shared(shared) {}
public slots:
    void OnPropertiesChanged(const QDBusMessage& message)
    {
        const auto arguments = message.arguments();
        if (arguments.size() < 2 || arguments.at(0).toString() != QLatin1String("org.bluez.Device1")) return;
        const QVariantMap changed = qdbus_cast<QVariantMap>(arguments.at(1));
        for (const char* key : {"Paired", "Connected", "Trusted"})
            if (changed.contains(QLatin1String(key)))
                m_shared->Log("INFO", "Device " + Text(message.path()) + ": " + key + " = " + Text(changed.value(QLatin1String(key)).toString()));
    }
private:
    Shared* m_shared;
};
}   // namespace bluez

using namespace bluez;
namespace {
constexpr const char* kService = "org.bluez";
constexpr const char* kAdapterPath = "/org/bluez/hci0";
constexpr const char* kAgentPath = "/org/headunit/agent";
constexpr const char* kProfilePath = "/org/headunit/aawireless";

std::string ErrorText(const QDBusMessage& reply) { return Text(reply.errorName()) + ": " + Text(reply.errorMessage()); }

// Empty when the call worked.
std::string Call(QDBusConnection& bus, const char* path, const char* interface, const char* method, const QList<QVariant>& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(path), QLatin1String(interface), QLatin1String(method));
    message.setArguments(arguments);
    const auto reply = bus.call(message, QDBus::Block, 8000);
    return reply.type() == QDBusMessage::ErrorMessage ? ErrorText(reply) : std::string();
}

std::string SetAdapterProperty(QDBusConnection& bus, const char* name, const QVariant& value)
{
    return Call(bus, kAdapterPath, "org.freedesktop.DBus.Properties", "Set",
        {QStringLiteral("org.bluez.Adapter1"), QString::fromLatin1(name), QVariant::fromValue(QDBusVariant(value))});
}
}

struct BluetoothService::Impl {
    explicit Impl(Logger& logger) { shared.logger = &logger; }
    Shared shared;
    std::unique_ptr<ExportedObject> agent, profile, watcher;
    bool isRunning{};
};

BluetoothService::BluetoothService(Logger& logger) : m_impl(std::make_unique<Impl>(logger)) {}
BluetoothService::~BluetoothService() { Stop(); }

std::string BluetoothService::Start(const std::string& name)
{
    Stop();
    if (!QCoreApplication::instance()) return "Interner Fehler: Bluetooth braucht eine Qt-Anwendung (D-Bus).";
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return "Kein Zugriff auf den System-D-Bus: " + Text(bus.lastError().message());
    auto& shared = m_impl->shared;

    // The adapter has to be on before anything else can be set. It is often switched off by rfkill on a fresh system.
    if (const auto error = SetAdapterProperty(bus, "Powered", true); !error.empty())
        return "Der Bluetooth-Adapter hci0 laesst sich nicht einschalten (" + error + "). Laeuft der Dienst (systemctl status bluetooth) "
            "und ist Bluetooth frei (rfkill unblock bluetooth)? Pruefen mit: bluetoothctl show";
    const std::vector<std::pair<const char*, QVariant>> properties{{"Alias", QString::fromStdString(name)}, {"Pairable", true},
        {"PairableTimeout", QVariant::fromValue<quint32>(0)}, {"Discoverable", true}, {"DiscoverableTimeout", QVariant::fromValue<quint32>(0)}};
    for (const auto& [property, value] : properties)
        if (const auto error = SetAdapterProperty(bus, property, value); !error.empty()) shared.Log("WARN", std::string("Adapter property ") + property + ": " + error);

    m_impl->agent = std::make_unique<ExportedObject>(&shared);
    new AgentAdaptor(m_impl->agent.get(), &shared);
    m_impl->profile = std::make_unique<ExportedObject>(&shared);
    new ProfileAdaptor(m_impl->profile.get(), &shared);
    m_impl->watcher = std::make_unique<ExportedObject>(&shared);
    if (!bus.registerObject(QLatin1String(kAgentPath), m_impl->agent.get()) || !bus.registerObject(QLatin1String(kProfilePath), m_impl->profile.get()))
        return "Die D-Bus-Objekte fuer Bluetooth lassen sich nicht anmelden: " + Text(bus.lastError().message());
    bus.connect(QLatin1String(kService), QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
        m_impl->watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));

    // The agent answers pairing requests; the phone pairs from its own Bluetooth settings.
    if (const auto error = Call(bus, "/org/bluez", "org.bluez.AgentManager1", "RegisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("NoInputNoOutput")}); !error.empty())
        shared.Log("WARN", "Pairing agent not registered: " + error);
    else if (const auto defaultError = Call(bus, "/org/bluez", "org.bluez.AgentManager1", "RequestDefaultAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))}); !defaultError.empty())
        shared.Log("WARN", "Pairing agent is not the default: " + defaultError);

    QVariantMap options;
    options.insert(QStringLiteral("Name"), QStringLiteral("Android Auto Wireless"));
    options.insert(QStringLiteral("Role"), QStringLiteral("server"));
    options.insert(QStringLiteral("RequireAuthentication"), false);
    options.insert(QStringLiteral("RequireAuthorization"), false);
    if (const auto error = Call(bus, "/org/bluez", "org.bluez.ProfileManager1", "RegisterProfile",
            {QVariant::fromValue(QDBusObjectPath(kProfilePath)), QString::fromLatin1(kAndroidAutoWirelessUuid), options}); !error.empty()) {
        Stop();
        return "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden: " + error;
    }
    m_impl->isRunning = true;
    shared.Log("INFO", "Bluetooth visible as '" + name + "', Android Auto Wireless service registered");
    return {};
}

void BluetoothService::Stop()
{
    auto& impl = *m_impl;
    if (impl.agent || impl.profile) {
        auto bus = QDBusConnection::systemBus();
        if (bus.isConnected()) {
            Call(bus, "/org/bluez", "org.bluez.ProfileManager1", "UnregisterProfile", {QVariant::fromValue(QDBusObjectPath(kProfilePath))});
            Call(bus, "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))});
            if (impl.isRunning) SetAdapterProperty(bus, "Discoverable", false);
            bus.unregisterObject(QLatin1String(kAgentPath));
            bus.unregisterObject(QLatin1String(kProfilePath));
            if (impl.watcher) bus.disconnect(QLatin1String(kService), QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
                impl.watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));
        }
    }
    impl.watcher.reset();
    impl.agent.reset();
    impl.profile.reset();
    for (const int fd : impl.shared.phones) ::close(fd);
    impl.shared.phones.clear();
    if (impl.isRunning) impl.shared.Log("INFO", "Bluetooth service stopped");
    impl.isRunning = false;
}

int BluetoothService::WaitForPhone(std::chrono::milliseconds timeout)
{
    auto& phones = m_impl->shared.phones;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (phones.empty()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) return -1;
        // The calls from BlueZ arrive as events of this thread.
        QEventLoop loop;
        QTimer::singleShot(static_cast<int>(std::min<long long>(left, 100)), &loop, &QEventLoop::quit);
        loop.exec();
    }
    const int fd = phones.front();
    phones.pop_front();
    return fd;
}
}

#include "BluetoothService.moc"
