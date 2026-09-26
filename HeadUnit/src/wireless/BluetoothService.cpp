#include "wireless/BluetoothService.h"
#include "wireless/SystemCommand.h"
#include "wireless/WirelessProtocol.h"
#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/rfkill.h>
#include <mutex>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace headunit {
namespace bluez {
constexpr const char* kService = "org.bluez";

// What the D-Bus objects share with the service. The objects run on the service's thread; the phones are handed over
// to whichever thread waits in WaitForPhone.
struct Shared {
    Logger* logger{};
    std::function<void(const std::string&)> onEvent;
    std::mutex mutex;
    std::condition_variable hasPhone;
    std::deque<int> phones;   // connected RFCOMM sockets of phones that opened the Android Auto service
    void Log(const std::string& level, const std::string& message) const { if (logger) logger->Write(level, "BT", message); }
    void Tell(const std::string& text) const { if (onEvent) onEvent(text); }
    void AddPhone(int fd)
    {
        { std::lock_guard lock(mutex); phones.push_back(fd); }
        hasPhone.notify_all();
    }
};

static std::string Text(const QString& value) { return value.toStdString(); }

// The name the phone gave itself ("Galaxy Z Flip5"), for the window; the device path when BlueZ does not answer.
static std::string DeviceName(const QString& devicePath)
{
    auto message = QDBusMessage::createMethodCall(QLatin1String(kService), devicePath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    message.setArguments({QStringLiteral("org.bluez.Device1"), QStringLiteral("Alias")});
    const auto reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return Text(devicePath);
    const QVariant value = reply.arguments().at(0);
    const auto name = value.userType() == qMetaTypeId<QDBusVariant>() ? qvariant_cast<QDBusVariant>(value).variant().toString() : value.toString();
    return name.isEmpty() ? Text(devicePath) : Text(name);
}

// Marks a device as trusted: BlueZ then lets it reconnect and use its services without asking this program again
// (those questions would wait unanswered while a session runs).
static void Trust(const QString& devicePath)
{
    auto message = QDBusMessage::createMethodCall(QLatin1String(kService), devicePath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Set"));
    message.setArguments({QStringLiteral("org.bluez.Device1"), QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))});
    QDBusConnection::systemBus().send(message);   // the answer is not needed
}

// Pairing as in a car: BlueZ is told that this side has a display and a yes/no button ("DisplayYesNo"), so the phone
// and the head unit compare a six-digit code (numeric comparison). The head unit confirms by itself and shows the code;
// the person confirms on the phone. Just Works pairing (no code, "NoInputNoOutput") is not used: BlueZ refuses it for a
// phone it has paired before (JustWorksRepairing = never, its default), so a phone that forgot the pairing and pairs
// again got "pairing not done". A code comparison is not affected by that rule.
class AgentAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")
public:
    AgentAdaptor(QObject* parent, Shared* shared) : QDBusAbstractAdaptor(parent), m_shared(shared) {}
public slots:
    void Release() { m_shared->Log("INFO", "Pairing agent released by BlueZ"); }
    // Only phones without Secure Simple Pairing (none from the last decade) ask for a PIN.
    QString RequestPinCode(const QDBusObjectPath& device) { m_shared->Log("INFO", "PIN requested by " + Text(device.path()) + "; answering 0000"); return QStringLiteral("0000"); }
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pin) { m_shared->Log("INFO", "PIN for " + Text(device.path()) + ": " + Text(pin)); }
    quint32 RequestPasskey(const QDBusObjectPath& device) { m_shared->Log("INFO", "Passkey requested by " + Text(device.path()) + "; answering 0"); return 0; }
    void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16)
    {
        m_shared->Log("INFO", "Passkey for " + Text(device.path()) + ": " + Code(passkey));
        m_shared->Tell("Bluetooth-Kopplung: am Handy den Code " + Code(passkey) + " eingeben.");
    }
    void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey)
    {
        const auto name = DeviceName(device.path());
        m_shared->Log("INFO", "Pairing with '" + name + "' (" + Text(device.path()) + ") confirmed, code " + Code(passkey));
        m_shared->Tell("Bluetooth-Kopplung mit " + name + ": am Handy den Code " + Code(passkey) + " bestaetigen.");
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
    static std::string Code(quint32 passkey)
    {
        char text[16]{};
        std::snprintf(text, sizeof(text), "%06u", static_cast<unsigned>(passkey));
        return text;
    }
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
        m_shared->AddPhone(own);
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
        if (changed.value(QStringLiteral("Paired")).toBool()) {
            Trust(message.path());
            m_shared->Tell("Bluetooth: " + DeviceName(message.path()) + " ist gekoppelt. Android Auto meldet sich jetzt am Handy.");
        }
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
    Impl(Logger& logger, std::function<void(const std::string&)> onEvent)
    {
        shared.logger = &logger;
        shared.onEvent = std::move(onEvent);
    }
    Shared shared;
    // The thread that answers BlueZ, and an object living on it that carries the work handed to it.
    std::unique_ptr<QThread> thread;
    std::unique_ptr<QObject> context;
    std::atomic_bool isRunning{};
    // Only used on that thread:
    QString adapterPath;
    std::unique_ptr<ExportedObject> agent, profile, watcher;

    // Runs `work` on the service's thread and returns when it is done.
    template <class Work> void RunThere(Work&& work)
    {
        QMetaObject::invokeMethod(context.get(), std::forward<Work>(work), Qt::BlockingQueuedConnection);
    }
    std::string StartHere(const std::string& name);
    void StopHere();
    void ConnectPairedHere();
    void ConnectPhones(const std::vector<PairedDevice>& phones);
};

std::string BluetoothService::Impl::StartHere(const std::string& name)
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return "Kein Zugriff auf den System-D-Bus: " + Text(bus.lastError().message());

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
    const QString adapter = adapterPath = bluez.adapterPath;
    std::string paired;
    for (const auto& device : bluez.paired) paired += (paired.empty() ? "" : ", ") + device.name + (device.isConnected ? " (connected)" : "");
    shared.Log("INFO", "Adapter " + Text(adapter) + "; paired devices: " + (paired.empty() ? "none" : paired));

    if (const auto error = PowerOn(bus, adapter, shared); !error.empty()) return error;
    if (const auto error = SetAdapterProperty(bus, adapter, "Alias", QString::fromStdString(name)); !error.empty()) shared.Log("WARN", "Adapter name: " + error);

    agent = std::make_unique<ExportedObject>(&shared);
    new AgentAdaptor(agent.get(), &shared);
    profile = std::make_unique<ExportedObject>(&shared);
    new ProfileAdaptor(profile.get(), &shared);
    watcher = std::make_unique<ExportedObject>(&shared);
    if (!bus.registerObject(QLatin1String(kAgentPath), agent.get()) || !bus.registerObject(QLatin1String(kProfilePath), profile.get()))
        return "Die D-Bus-Objekte fuer Bluetooth lassen sich nicht anmelden: " + Text(bus.lastError().message());
    bus.connect(QLatin1String(kService), QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
        watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));

    // The agent answers pairing requests; the phone pairs from its own Bluetooth settings. It is the default agent, so
    // that the desktop's Bluetooth applet does not ask on the Pi's screen instead.
    const QString root = QStringLiteral("/org/bluez");
    if (const auto error = Call(bus, root, "org.bluez.AgentManager1", "RegisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath)), QStringLiteral("DisplayYesNo")}); !error.empty())
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
            {QVariant::fromValue(QDBusObjectPath(kProfilePath)), QString::fromLatin1(kAndroidAutoWirelessUuid), options}); !error.empty())
        return "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden: " + error;

    // Visible only now, with the agent and the service in place: a phone that pairs at once already finds the Android Auto
    // service (and then offers Android Auto by itself), and its pairing request reaches this agent.
    const std::vector<std::pair<const char*, QVariant>> properties{{"Pairable", true}, {"PairableTimeout", QVariant::fromValue<quint32>(0)},
        {"DiscoverableTimeout", QVariant::fromValue<quint32>(0)}, {"Discoverable", true}};
    for (const auto& [property, value] : properties)
        if (const auto error = SetAdapterProperty(bus, adapter, property, value); !error.empty()) shared.Log("WARN", std::string("Adapter property ") + property + ": " + error);
    isRunning = true;
    shared.Log("INFO", "Bluetooth visible as '" + name + "', pairing with code comparison, Android Auto Wireless service registered on RFCOMM channel " +
        std::to_string(kAndroidAutoWirelessChannel));
    return {};
}

void BluetoothService::Impl::StopHere()
{
    auto bus = QDBusConnection::systemBus();
    if ((agent || profile) && bus.isConnected()) {
        const QString root = QStringLiteral("/org/bluez");
        Call(bus, root, "org.bluez.ProfileManager1", "UnregisterProfile", {QVariant::fromValue(QDBusObjectPath(kProfilePath))});
        Call(bus, root, "org.bluez.AgentManager1", "UnregisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))});
        if (isRunning) SetAdapterProperty(bus, adapterPath, "Discoverable", false);
        bus.unregisterObject(QLatin1String(kAgentPath));
        bus.unregisterObject(QLatin1String(kProfilePath));
        if (watcher) bus.disconnect(QLatin1String(kService), QString(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"),
            watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));
    }
    watcher.reset();
    agent.reset();
    profile.reset();
    if (isRunning) shared.Log("INFO", "Bluetooth service stopped");
    isRunning = false;
}

void BluetoothService::Impl::ConnectPairedHere()
{
    if (!isRunning) return;
    auto bus = QDBusConnection::systemBus();
    std::vector<PairedDevice> phones;
    bool isAnyDisconnected = false;
    for (const auto& device : ReadBluez(bus).paired) {
        if (!device.isPhone) continue;
        phones.push_back(device);
        if (!device.isConnected) continue;
        shared.Log("INFO", "Phone '" + device.name + "' was connected before the Android Auto service existed; reconnecting it");
        if (const auto error = Call(bus, device.path, "org.bluez.Device1", "Disconnect", {}); !error.empty())
            shared.Log("WARN", "Disconnecting '" + device.name + "' failed: " + error);
        isAnyDisconnected = true;
    }
    // The old link needs a moment to go down before a new one can start; BlueZ is answered meanwhile.
    if (isAnyDisconnected) QTimer::singleShot(2000, context.get(), [this, phones] { ConnectPhones(phones); });
    else ConnectPhones(phones);
}

void BluetoothService::Impl::ConnectPhones(const std::vector<PairedDevice>& phones)
{
    auto bus = QDBusConnection::systemBus();
    for (const auto& device : phones) {
        shared.Log("INFO", "Asking the paired phone '" + device.name + "' to connect");
        // Takes seconds, or fails when the phone is out of reach, so the answer is only logged when it comes; the phone
        // then opens the Android Auto service by itself.
        const auto message = QDBusMessage::createMethodCall(QLatin1String(kService), device.path, QStringLiteral("org.bluez.Device1"), QStringLiteral("Connect"));
        auto* call = new QDBusPendingCallWatcher(bus.asyncCall(message, 30000), context.get());
        QObject::connect(call, &QDBusPendingCallWatcher::finished, context.get(), [this, name = device.name](QDBusPendingCallWatcher* finished) {
            if (finished->isError())
                shared.Log("INFO", "Paired phone '" + name + "' did not connect (" + Text(finished->error().name()) + ": " + Text(finished->error().message()) +
                    "); it can still connect by itself");
            else shared.Log("INFO", "Paired phone '" + name + "' connected");
            finished->deleteLater();
        });
    }
}

BluetoothService::BluetoothService(Logger& logger, std::function<void(const std::string&)> onEvent)
    : m_impl(std::make_unique<Impl>(logger, std::move(onEvent))) {}
BluetoothService::~BluetoothService() { Stop(); }

std::string BluetoothService::Start(const std::string& name)
{
    Stop();
    if (!QCoreApplication::instance()) return "Interner Fehler: Bluetooth braucht eine Qt-Anwendung (D-Bus).";
    auto& impl = *m_impl;
    impl.thread = std::make_unique<QThread>();
    impl.thread->setObjectName(QStringLiteral("Bluetooth"));
    impl.context = std::make_unique<QObject>();
    impl.context->moveToThread(impl.thread.get());
    impl.thread->start();
    std::string error;
    impl.RunThere([&] {
        error = impl.StartHere(name);
        if (!error.empty()) impl.StopHere();
    });
    if (!error.empty()) Stop();
    return error;
}

void BluetoothService::Stop()
{
    auto& impl = *m_impl;
    if (!impl.thread) return;
    impl.RunThere([&] { impl.StopHere(); });
    impl.thread->quit();
    impl.thread->wait();
    // The thread has ended, so its object (and the calls still pending on it) can go from here.
    impl.context.reset();
    impl.thread.reset();
    std::lock_guard lock(impl.shared.mutex);
    for (const int fd : impl.shared.phones) ::close(fd);
    impl.shared.phones.clear();
}

bool BluetoothService::IsRunning() const { return m_impl->isRunning; }

void BluetoothService::ConnectPairedPhones()
{
    auto& impl = *m_impl;
    if (!impl.isRunning) return;
    QMetaObject::invokeMethod(impl.context.get(), [&impl] { impl.ConnectPairedHere(); }, Qt::QueuedConnection);
}

int BluetoothService::WaitForPhone(std::chrono::milliseconds timeout)
{
    auto& shared = m_impl->shared;
    std::unique_lock lock(shared.mutex);
    if (!shared.hasPhone.wait_for(lock, timeout, [&] { return !shared.phones.empty(); })) return -1;
    const int fd = shared.phones.front();
    shared.phones.pop_front();
    return fd;
}
}

#include "BluetoothService.moc"
