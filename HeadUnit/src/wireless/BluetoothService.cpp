#include "wireless/BluetoothService.h"
#include "wireless/SystemCommand.h"
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
#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/rfkill.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace headunit {
namespace bluez {
constexpr const char* kService = "org.bluez";

// What the D-Bus objects share with the service. Everything runs on the thread that calls the service.
struct Shared {
    Logger* logger{};
    std::deque<int> phones;   // connected RFCOMM sockets of phones that opened the Android Auto service
    void Log(const std::string& level, const std::string& message) const { if (logger) logger->Write(level, "BT", message); }
};

static std::string Text(const QString& value) { return value.toStdString(); }

// Marks a device as trusted: BlueZ then lets it reconnect and use its services without asking this program again
// (those questions would wait unanswered while a session runs).
static void Trust(const QString& devicePath)
{
    auto message = QDBusMessage::createMethodCall(QLatin1String(kService), devicePath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    message.setArguments({QStringLiteral("org.bluez.Device1"), QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))});
    QDBusConnection::systemBus().send(message);   // the answer is not needed
}

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
    void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey)
    {
        m_shared->Log("INFO", "Pairing with " + Text(device.path()) + " confirmed (code " + std::to_string(passkey) + ")");
        Trust(device.path());
    }
    void RequestAuthorization(const QDBusObjectPath& device) { m_shared->Log("INFO", "Pairing authorized for " + Text(device.path())); Trust(device.path()); }
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid)
    {
        m_shared->Log("INFO", "Service " + Text(uuid) + " authorized for " + Text(device.path()));
        Trust(device.path());
    }
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
        Trust(device.path());
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
        if (changed.value(QStringLiteral("Paired")).toBool()) Trust(message.path());
    }
private:
    Shared* m_shared;
};
}   // namespace bluez

using namespace bluez;
namespace {
using namespace std::chrono_literals;
constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
constexpr const char* kAgentPath = "/org/headunit/agent";
constexpr const char* kProfilePath = "/org/headunit/aawireless";

std::string ErrorText(const QDBusMessage& reply) { return Text(reply.errorName()) + ": " + Text(reply.errorMessage()); }

QDBusMessage CallRaw(QDBusConnection& bus, const QString& path, const char* interface, const char* method, const QList<QVariant>& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kService), path, QLatin1String(interface), QLatin1String(method));
    message.setArguments(arguments);
    return bus.call(message, QDBus::Block, 8000);
}

// Empty when the call worked.
std::string Call(QDBusConnection& bus, const QString& path, const char* interface, const char* method, const QList<QVariant>& arguments)
{
    const auto reply = CallRaw(bus, path, interface, method, arguments);
    return reply.type() == QDBusMessage::ErrorMessage ? ErrorText(reply) : std::string();
}

std::string SetAdapterProperty(QDBusConnection& bus, const QString& adapter, const char* name, const QVariant& value)
{
    return Call(bus, adapter, "org.freedesktop.DBus.Properties", "Set",
        {QString::fromLatin1(kAdapterInterface), QString::fromLatin1(name), QVariant::fromValue(QDBusVariant(value))});
}

bool IsPowered(QDBusConnection& bus, const QString& adapter)
{
    const auto reply = CallRaw(bus, adapter, "org.freedesktop.DBus.Properties", "Get", {QString::fromLatin1(kAdapterInterface), QStringLiteral("Powered")});
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return false;
    // Get answers with a variant, which QtDBus hands over wrapped in a QDBusVariant.
    const QVariant value = reply.arguments().at(0);
    if (value.userType() == qMetaTypeId<QDBusVariant>()) return qvariant_cast<QDBusVariant>(value).variant().toBool();
    return value.toBool();
}

struct PairedDevice {
    QString path;
    std::string name;
    bool isConnected{};
    bool isPhone{};   // BlueZ's icon for the device class "phone"
};
// What BlueZ knows: whether it runs at all, its first adapter, and the devices paired so far.
struct BluezState {
    bool isRunning{};
    std::string error;
    QString adapterPath;
    std::vector<PairedDevice> paired;
};
BluezState ReadBluez(QDBusConnection& bus)
{
    BluezState state;
    const auto reply = CallRaw(bus, QStringLiteral("/"), "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {});
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) { state.error = ErrorText(reply); return state; }
    state.isRunning = true;
    std::vector<QString> adapters;
    const auto objects = qvariant_cast<QDBusArgument>(reply.arguments().at(0));   // a{oa{sa{sv}}}
    objects.beginMap();
    while (!objects.atEnd()) {
        objects.beginMapEntry();
        QDBusObjectPath path;
        objects >> path;
        objects.beginMap();
        while (!objects.atEnd()) {
            objects.beginMapEntry();
            QString interfaceName;
            QVariantMap properties;
            objects >> interfaceName >> properties;
            objects.endMapEntry();
            if (interfaceName == QLatin1String(kAdapterInterface)) adapters.push_back(path.path());
            else if (interfaceName == QLatin1String("org.bluez.Device1") && properties.value(QStringLiteral("Paired")).toBool())
                state.paired.push_back({path.path(), Text(properties.value(QStringLiteral("Alias")).toString()),
                    properties.value(QStringLiteral("Connected")).toBool(), properties.value(QStringLiteral("Icon")).toString() == QLatin1String("phone")});
        }
        objects.endMap();
        objects.endMapEntry();
    }
    objects.endMap();
    std::sort(adapters.begin(), adapters.end());   // hci0 first
    if (!adapters.empty()) state.adapterPath = adapters.front();
    return state;
}

struct RfkillState { bool isPresent{}, isSoftBlocked{}, isHardBlocked{}; };
RfkillState ReadBluetoothRfkill()
{
    RfkillState state;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/rfkill", error)) {
        const auto read = [&](const char* name) { std::ifstream file(entry.path() / name); std::string value; std::getline(file, value); return value; };
        if (read("type") != "bluetooth") continue;
        state.isPresent = true;
        state.isSoftBlocked |= read("soft") == "1";
        state.isHardBlocked |= read("hard") == "1";
    }
    return state;
}

// What `rfkill unblock bluetooth` does. /dev/rfkill is writable for the user logged in at the screen (systemd's uaccess
// rule); over SSH it usually is not, and then sudo without a password (Raspberry Pi OS's first user has it) is tried.
bool UnblockBluetooth(const Shared& shared)
{
    if (const int fd = ::open("/dev/rfkill", O_RDWR | O_CLOEXEC); fd >= 0) {
        rfkill_event event{};
        event.type = RFKILL_TYPE_BLUETOOTH;
        event.op = RFKILL_OP_CHANGE_ALL;
        event.soft = 0;
        const auto written = ::write(fd, &event, sizeof(event));
        const int writeError = errno;
        ::close(fd);
        if (written == static_cast<ssize_t>(sizeof(event))) { shared.Log("INFO", "Bluetooth unblocked through /dev/rfkill"); return true; }
        shared.Log("INFO", std::string("/dev/rfkill is not writable: ") + std::strerror(writeError));
    } else {
        shared.Log("INFO", std::string("/dev/rfkill cannot be opened: ") + std::strerror(errno));
    }
    const auto command = RunCommand({"sudo", "-n", "rfkill", "unblock", "bluetooth"}, 10s);
    if (command.exitCode == 0) { shared.Log("INFO", "Bluetooth unblocked with sudo rfkill"); return true; }
    shared.Log("WARN", "sudo rfkill unblock bluetooth failed: " + (command.isStarted ? command.output : std::string("sudo not available")));
    return false;
}

// Empty when the adapter is on.
std::string PowerOn(QDBusConnection& bus, const QString& adapter, const Shared& shared)
{
    if (IsPowered(bus, adapter)) return {};
    auto error = SetAdapterProperty(bus, adapter, "Powered", true);
    if (error.empty() && IsPowered(bus, adapter)) { shared.Log("INFO", "Bluetooth adapter switched on"); return {}; }
    const auto rfkill = ReadBluetoothRfkill();
    shared.Log("INFO", "Bluetooth adapter is off (" + (error.empty() ? std::string("stays off") : error) + "); rfkill: " +
        (!rfkill.isPresent ? "no entry" : rfkill.isHardBlocked ? "hard blocked" : rfkill.isSoftBlocked ? "soft blocked" : "not blocked"));
    if (rfkill.isHardBlocked)
        return "Bluetooth ist per Hardware gesperrt (rfkill hard block) und laesst sich von hier nicht einschalten. Pruefen mit: rfkill list";
    if (rfkill.isSoftBlocked && !UnblockBluetooth(shared))
        return "Bluetooth ist gesperrt (rfkill) und liess sich nicht entsperren. Einmalig im Terminal: sudo rfkill unblock bluetooth";
    // After the unblock the adapter needs a moment before it can be switched on.
    for (int attempt = 0; attempt < 25; ++attempt) {
        std::this_thread::sleep_for(200ms);
        error = SetAdapterProperty(bus, adapter, "Powered", true);
        if (error.empty() && IsPowered(bus, adapter)) { shared.Log("INFO", "Bluetooth adapter switched on"); return {}; }
    }
    return "Der Bluetooth-Adapter laesst sich nicht einschalten (" + (error.empty() ? std::string("bleibt aus") : error) +
        "). Pruefen mit: bluetoothctl show und rfkill list";
}
}

struct BluetoothService::Impl {
    explicit Impl(Logger& logger) { shared.logger = &logger; }
    Shared shared;
    QString adapterPath;
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

    // BlueZ itself normally starts at boot; where it does not, it is started here (sudo without a password).
    auto bluez = ReadBluez(bus);
    if (!bluez.isRunning) {
        shared.Log("WARN", "BlueZ is not running (" + bluez.error + "); trying: sudo -n systemctl start bluetooth");
        RunCommand({"sudo", "-n", "systemctl", "start", "bluetooth"}, 20s);
        for (int attempt = 0; attempt < 25 && !bluez.isRunning; ++attempt) {
            std::this_thread::sleep_for(200ms);
            bluez = ReadBluez(bus);
        }
        if (!bluez.isRunning) return "Der Bluetooth-Dienst (bluetoothd) laeuft nicht. Einschalten mit: sudo systemctl enable --now bluetooth";
    }
    if (bluez.adapterPath.isEmpty()) return "Kein Bluetooth-Adapter gefunden. Pruefen mit: bluetoothctl list und rfkill list";
    const QString adapter = m_impl->adapterPath = bluez.adapterPath;
    std::string paired;
    for (const auto& device : bluez.paired) paired += (paired.empty() ? "" : ", ") + device.name + (device.isConnected ? " (connected)" : "");
    shared.Log("INFO", "Adapter " + Text(adapter) + "; paired devices: " + (paired.empty() ? "none" : paired));

    if (const auto error = PowerOn(bus, adapter, shared); !error.empty()) return error;
    const std::vector<std::pair<const char*, QVariant>> properties{{"Alias", QString::fromStdString(name)}, {"Pairable", true},
        {"PairableTimeout", QVariant::fromValue<quint32>(0)}, {"Discoverable", true}, {"DiscoverableTimeout", QVariant::fromValue<quint32>(0)}};
    for (const auto& [property, value] : properties)
        if (const auto error = SetAdapterProperty(bus, adapter, property, value); !error.empty()) shared.Log("WARN", std::string("Adapter property ") + property + ": " + error);

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
    const QString root = QStringLiteral("/org/bluez");
    if (const auto error = Call(bus, root, "org.bluez.AgentManager1", "RegisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("NoInputNoOutput")}); !error.empty())
        shared.Log("WARN", "Pairing agent not registered: " + error);
    else if (const auto defaultError = Call(bus, root, "org.bluez.AgentManager1", "RequestDefaultAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))}); !defaultError.empty())
        shared.Log("WARN", "Pairing agent is not the default: " + defaultError);

    QVariantMap options;
    options.insert(QStringLiteral("Name"), QStringLiteral("Android Auto Wireless"));
    options.insert(QStringLiteral("Role"), QStringLiteral("server"));
    options.insert(QStringLiteral("Channel"), QVariant::fromValue<quint16>(kAndroidAutoWirelessChannel));   // D-Bus uint16, as BlueZ requires
    options.insert(QStringLiteral("RequireAuthentication"), false);
    options.insert(QStringLiteral("RequireAuthorization"), false);
    if (const auto error = Call(bus, root, "org.bluez.ProfileManager1", "RegisterProfile",
            {QVariant::fromValue(QDBusObjectPath(kProfilePath)), QString::fromLatin1(kAndroidAutoWirelessUuid), options}); !error.empty()) {
        Stop();
        return "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden: " + error;
    }
    m_impl->isRunning = true;
    shared.Log("INFO", "Bluetooth visible as '" + name + "', Android Auto Wireless service registered on RFCOMM channel " +
        std::to_string(kAndroidAutoWirelessChannel));
    return {};
}

void BluetoothService::Stop()
{
    auto& impl = *m_impl;
    if (impl.agent || impl.profile) {
        auto bus = QDBusConnection::systemBus();
        if (bus.isConnected()) {
            const QString root = QStringLiteral("/org/bluez");
            Call(bus, root, "org.bluez.ProfileManager1", "UnregisterProfile", {QVariant::fromValue(QDBusObjectPath(kProfilePath))});
            Call(bus, root, "org.bluez.AgentManager1", "UnregisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))});
            if (impl.isRunning) SetAdapterProperty(bus, impl.adapterPath, "Discoverable", false);
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

void BluetoothService::ConnectPairedPhones()
{
    if (!m_impl->isRunning) return;
    auto bus = QDBusConnection::systemBus();
    const auto& shared = m_impl->shared;
    const auto paired = ReadBluez(bus).paired;
    bool isAnyDisconnected = false;
    for (const auto& device : paired) {
        if (!device.isPhone || !device.isConnected) continue;
        shared.Log("INFO", "Phone '" + device.name + "' was connected before the Android Auto service existed; reconnecting it");
        if (const auto error = Call(bus, device.path, "org.bluez.Device1", "Disconnect", {}); !error.empty())
            shared.Log("WARN", "Disconnecting '" + device.name + "' failed: " + error);
        isAnyDisconnected = true;
    }
    // The old link needs a moment to go down before a new one can start; BlueZ is answered meanwhile.
    if (isAnyDisconnected) Pump(2s);
    for (const auto& device : paired) {
        if (!device.isPhone) continue;
        shared.Log("INFO", "Asking the paired phone '" + device.name + "' to connect");
        // Takes seconds, or fails when the phone is out of reach, so the answer is not awaited: the log shows
        // "Connected = true" when it worked, and the phone then opens the Android Auto service by itself.
        bus.send(QDBusMessage::createMethodCall(QLatin1String(kService), device.path, QStringLiteral("org.bluez.Device1"), QStringLiteral("Connect")));
    }
}

void BluetoothService::Pump(std::chrono::milliseconds duration)
{
    // The calls from BlueZ arrive as events of this thread.
    QEventLoop loop;
    QTimer::singleShot(static_cast<int>(std::max<long long>(duration.count(), 1)), &loop, &QEventLoop::quit);
    loop.exec();
}

int BluetoothService::WaitForPhone(std::chrono::milliseconds timeout)
{
    auto& phones = m_impl->shared.phones;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (phones.empty()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return -1;
        Pump(std::min<std::chrono::milliseconds>(left, 100ms));
    }
    const int fd = phones.front();
    phones.pop_front();
    return fd;
}
}

#include "BluetoothService.moc"
