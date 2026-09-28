#include "wireless/BluetoothService.h"
#include "wireless/AndroidAutoProfile.h"
#include "wireless/DeviceWatcher.h"
#include "wireless/PairingAgent.h"
#include "wireless/SystemCommand.h"
#include "wireless/WirelessProtocol.h"
#include <QCoreApplication>
#include <QDBusError>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <thread>
#include <utility>

namespace headunit {
namespace {
using namespace std::chrono_literals;
using bluez::Text;
constexpr const char* kAgentPath = "/org/headunit/agent";
constexpr const char* kProfilePath = "/org/headunit/aawireless";
constexpr const char* kBluezRoot = "/org/bluez";

// BlueZ's state. BlueZ normally starts at boot; where it does not, it is started here (sudo without a password).
bluez::BluezState ReadStartedBluez(QDBusConnection& bus, const BluetoothContext& context)
{
    auto state = bluez::ReadBluez(bus);
    if (state.isRunning) return state;
    context.Log(LogLevel::Warning, "BlueZ is not running (" + state.error + "); trying: sudo -n systemctl start bluetooth");
    RunCommand({"sudo", "-n", "systemctl", "start", "bluetooth"}, 20s);
    for (int attempt = 0; attempt < 25 && !state.isRunning; ++attempt) {
        std::this_thread::sleep_for(200ms);
        state = bluez::ReadBluez(bus);
    }
    return state;
}

// The service's options for ProfileManager1.RegisterProfile on `channel`.
QVariantMap ProfileOptions(quint16 channel)
{
    QVariantMap options;
    options.insert(QStringLiteral("Name"), QStringLiteral("Android Auto Wireless"));
    options.insert(QStringLiteral("Role"), QStringLiteral("server"));
    options.insert(QStringLiteral("RequireAuthentication"), false);
    options.insert(QStringLiteral("RequireAuthorization"), false);
    options.insert(QStringLiteral("Channel"), QVariant::fromValue<quint16>(channel));   // D-Bus uint16, as BlueZ requires
    return options;
}

// Registers the Android Auto Wireless service on `channel`; empty when BlueZ accepted it.
std::string RegisterProfileOn(QDBusConnection& bus, quint16 channel)
{
    return bluez::Call(bus, QString::fromLatin1(kBluezRoot), "org.bluez.ProfileManager1", "RegisterProfile",
        {QVariant::fromValue(QDBusObjectPath(kProfilePath)), QString::fromLatin1(kAndroidAutoWirelessUuid), ProfileOptions(channel)});
}

// Removes the Android Auto Wireless service from BlueZ.
void UnregisterProfile(QDBusConnection& bus)
{
    bluez::Call(bus, QString::fromLatin1(kBluezRoot), "org.bluez.ProfileManager1", "UnregisterProfile", {QVariant::fromValue(QDBusObjectPath(kProfilePath))});
}
}

// A service that is not running yet.
BluetoothService::BluetoothService(Logger& logger, BluetoothEvents events) : m_context(logger, std::move(events))
{
}

// Stops the service.
BluetoothService::~BluetoothService()
{
    Stop();
}

// Returns when Bluetooth is visible. Empty on success, otherwise what went wrong and what to do about it (German, for
// the window).
std::string BluetoothService::Start(const std::string& name)
{
    Stop();
    if (!QCoreApplication::instance()) return "Interner Fehler: Bluetooth braucht eine Qt-Anwendung (D-Bus).";
    m_thread = std::make_unique<QThread>();
    m_thread->setObjectName(QStringLiteral("Bluetooth"));
    m_worker = std::make_unique<QObject>();
    m_worker->moveToThread(m_thread.get());
    m_thread->start();
    std::string error;
    RunOnServiceThread([&] {
        error = StartOnServiceThread(name);
        if (!error.empty()) StopOnServiceThread();
    });
    if (!error.empty()) Stop();
    return error;
}

// Takes the service off BlueZ, ends its thread and closes the sockets of phones nobody took.
void BluetoothService::Stop()
{
    if (!m_thread) return;
    RunOnServiceThread([this] { StopOnServiceThread(); });
    m_thread->quit();
    m_thread->wait();
    // The thread has ended, so its object (and the calls still pending on it) can go from here.
    m_worker.reset();
    m_thread.reset();
    m_context.CloseWaitingPhones();
}

// Whether Bluetooth is visible with the Android Auto service.
bool BluetoothService::IsRunning() const
{
    return m_isRunning;
}

// Asks the phones paired before to connect, as a car does when it starts, and returns at once. Android Auto looks for its
// service when the Bluetooth connection starts, so a phone that is connected already is disconnected first: with
// `isReconnectingAll` every connected phone (the person asked for Android Auto), otherwise only those that were
// connected before the service existed (PipeWire connects calls and audio by itself, also before HeadUnit runs). Only
// devices BlueZ shows as phones. Call it once the head unit is ready for the phone (Wi-Fi up).
void BluetoothService::ConnectPairedPhones(bool isReconnectingAll)
{
    if (!m_isRunning) return;
    QMetaObject::invokeMethod(m_worker.get(), [this, isReconnectingAll] { ConnectPairedOnServiceThread(isReconnectingAll); }, Qt::QueuedConnection);
}

// Waits up to `timeout` for a phone that opened the Android Auto Wireless service. Returns its connected RFCOMM socket,
// which the caller owns and closes, or -1.
int BluetoothService::WaitForPhone(std::chrono::milliseconds timeout)
{
    return m_context.WaitForPhone(timeout);
}

// Runs `work` on the service's thread and returns when it is done.
void BluetoothService::RunOnServiceThread(const std::function<void()>& work)
{
    QMetaObject::invokeMethod(m_worker.get(), [&work] { work(); }, Qt::BlockingQueuedConnection);
}

// Switches Bluetooth on, puts the agent and the service in place and makes the adapter visible (service thread).
std::string BluetoothService::StartOnServiceThread(const std::string& name)
{
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return "Kein Zugriff auf den System-D-Bus: " + Text(bus.lastError().message());
    const auto state = ReadStartedBluez(bus, m_context);
    if (!state.isRunning) return "Der Bluetooth-Dienst (bluetoothd) laeuft nicht. Einschalten mit: sudo systemctl enable --now bluetooth";
    if (state.adapterPath.isEmpty()) return "Kein Bluetooth-Adapter gefunden. Pruefen mit: bluetoothctl list und rfkill list";
    m_adapterPath = state.adapterPath;

    std::string paired;
    m_context.ClearConnectedBefore();
    for (const auto& device : state.paired) {
        paired += (paired.empty() ? "" : ", ") + device.name + (device.isConnected ? " (connected)" : "");
        if (device.isPhone && device.isConnected) m_context.RememberConnectedBefore(device.path);
    }
    m_context.Log(LogLevel::Info, "Adapter " + Text(m_adapterPath) + "; paired devices: " + (paired.empty() ? "none" : paired));

    if (const auto error = bluez::PowerOn(bus, m_adapterPath, m_context); !error.empty()) return error;
    if (const auto error = bluez::SetAdapterProperty(bus, m_adapterPath, "Alias", QString::fromStdString(name)); !error.empty())
        m_context.Log(LogLevel::Warning, "Adapter name: " + error);
    if (const auto error = RegisterObjects(bus); !error.empty()) return error;
    RegisterAgent(bus);
    quint16 channel = 0;
    bool isListed = false;
    if (const auto error = RegisterProfile(bus, channel, isListed); !error.empty()) return error;
    MakeVisible(bus);
    m_isRunning = true;
    m_context.Log(LogLevel::Info, "Bluetooth visible as '" + name + "', pairing with code comparison, Android Auto Wireless service registered on RFCOMM channel " +
        std::to_string(channel) + (isListed ? " (listed in the adapter's services)" : " (NOT listed in the adapter's services: phones may not find it; see journalctl -u bluetooth)"));
    return {};
}

// Puts the agent, the service and the device watcher on the bus (service thread).
std::string BluetoothService::RegisterObjects(QDBusConnection& bus)
{
    m_agentObject = std::make_unique<QObject>();
    new PairingAgent(m_agentObject.get(), m_context);
    m_profileObject = std::make_unique<QObject>();
    new AndroidAutoProfile(m_profileObject.get(), m_context);
    m_watcher = std::make_unique<DeviceWatcher>(m_context);
    if (!bus.registerObject(QLatin1String(kAgentPath), m_agentObject.get()) || !bus.registerObject(QLatin1String(kProfilePath), m_profileObject.get()))
        return "Die D-Bus-Objekte fuer Bluetooth lassen sich nicht anmelden: " + Text(bus.lastError().message());
    bus.connect(QLatin1String(bluez::kService), QString(), QLatin1String(bluez::kPropertiesInterface), QStringLiteral("PropertiesChanged"),
        m_watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));
    return {};
}

// The agent answers pairing requests; the phone pairs from its own Bluetooth settings. It is the default agent, so that
// the desktop's Bluetooth applet does not ask on the Pi's screen instead (service thread).
void BluetoothService::RegisterAgent(QDBusConnection& bus)
{
    const QString root = QString::fromLatin1(kBluezRoot);
    const auto agentPath = QVariant::fromValue(QDBusObjectPath(kAgentPath));
    if (const auto error = bluez::Call(bus, root, "org.bluez.AgentManager1", "RegisterAgent", {agentPath, QStringLiteral("DisplayYesNo")}); !error.empty())
        m_context.Log(LogLevel::Warning, "Pairing agent not registered: " + error);
    else if (const auto defaultError = bluez::Call(bus, root, "org.bluez.AgentManager1", "RequestDefaultAgent", {agentPath}); !defaultError.empty())
        m_context.Log(LogLevel::Warning, "Pairing agent is not the default: " + defaultError);
}

// Registers the Android Auto Wireless service on the first free RFCOMM channel (service thread). BlueZ accepts the
// registration also when the channel is taken, and then publishes no service: the phone connects over Bluetooth, shows
// "connecting to Android Auto" and never finds the service. So every channel is checked in the adapter's list of
// services before it is kept; when none shows up there, the first channel is kept anyway, in case this BlueZ does not
// list the service at all.
std::string BluetoothService::RegisterProfile(QDBusConnection& bus, quint16& channel, bool& isListed)
{
    for (const quint16 candidate : kAndroidAutoWirelessChannels) {
        if (const auto error = RegisterProfileOn(bus, candidate); !error.empty()) return "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden: " + error;
        if (bluez::IsServiceListed(bus, m_adapterPath, kAndroidAutoWirelessUuid)) {
            channel = candidate;
            isListed = true;
            return {};
        }
        m_context.Log(LogLevel::Warning, "RFCOMM channel " + std::to_string(candidate) + " is taken (BlueZ published no Android Auto service on it); trying the next one");
        UnregisterProfile(bus);
    }
    m_context.Log(LogLevel::Warning, "The Android Auto service is listed on no RFCOMM channel");
    channel = kAndroidAutoWirelessChannels[0];
    isListed = false;
    if (const auto error = RegisterProfileOn(bus, channel); !error.empty()) return "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden: " + error;
    return {};
}

// Visible only now, with the agent and the service in place: a phone that pairs at once already finds the Android Auto
// service (and then offers Android Auto by itself), and its pairing request reaches this agent (service thread).
void BluetoothService::MakeVisible(QDBusConnection& bus)
{
    const std::vector<std::pair<const char*, QVariant>> properties{{"Pairable", true}, {"PairableTimeout", QVariant::fromValue<quint32>(0)},
        {"DiscoverableTimeout", QVariant::fromValue<quint32>(0)}, {"Discoverable", true}};
    for (const auto& [property, value] : properties) {
        if (const auto error = bluez::SetAdapterProperty(bus, m_adapterPath, property, value); !error.empty())
            m_context.Log(LogLevel::Warning, std::string("Adapter property ") + property + ": " + error);
    }
}

// Takes the agent, the service and the watcher off the bus and hides the adapter (service thread).
void BluetoothService::StopOnServiceThread()
{
    m_context.EndPairing(true);
    auto bus = QDBusConnection::systemBus();
    if ((m_agentObject || m_profileObject) && bus.isConnected()) {
        UnregisterProfile(bus);
        bluez::Call(bus, QString::fromLatin1(kBluezRoot), "org.bluez.AgentManager1", "UnregisterAgent", {QVariant::fromValue(QDBusObjectPath(kAgentPath))});
        if (m_isRunning) bluez::SetAdapterProperty(bus, m_adapterPath, "Discoverable", false);
        bus.unregisterObject(QLatin1String(kAgentPath));
        bus.unregisterObject(QLatin1String(kProfilePath));
        if (m_watcher)
            bus.disconnect(QLatin1String(bluez::kService), QString(), QLatin1String(bluez::kPropertiesInterface), QStringLiteral("PropertiesChanged"),
                m_watcher.get(), SLOT(OnPropertiesChanged(QDBusMessage)));
    }
    m_watcher.reset();
    m_agentObject.reset();
    m_profileObject.reset();
    if (m_isRunning) m_context.Log(LogLevel::Info, "Bluetooth service stopped");
    m_isRunning = false;
}

// See ConnectPairedPhones (service thread).
void BluetoothService::ConnectPairedOnServiceThread(bool isReconnectingAll)
{
    if (!m_isRunning) return;
    auto bus = QDBusConnection::systemBus();
    std::vector<bluez::PairedDevice> phones;
    bool isAnyDisconnected = false;
    for (const auto& device : bluez::ReadBluez(bus).paired) {
        if (!device.isPhone) continue;
        phones.push_back(device);
        // A phone that connected while the service existed has seen it; taking its connection down would only break an
        // Android Auto start that may be under way.
        if (!device.isConnected || !(isReconnectingAll || m_context.WasConnectedBefore(device.path))) continue;
        m_context.Log(LogLevel::Info, "Phone '" + device.name + (isReconnectingAll ? "' is connected; reconnecting it, so that Android Auto starts again"
                                                                                   : "' was connected before the Android Auto service existed; reconnecting it"));
        if (const auto error = bluez::Call(bus, device.path, bluez::kDeviceInterface, "Disconnect", {}); !error.empty())
            m_context.Log(LogLevel::Warning, "Disconnecting '" + device.name + "' failed: " + error);
        isAnyDisconnected = true;
    }
    m_context.ClearConnectedBefore();
    // The old link needs a moment to go down before a new one can start; BlueZ is answered meanwhile.
    if (isAnyDisconnected) QTimer::singleShot(2000, m_worker.get(), [this, phones] { ConnectPhones(phones); });
    else ConnectPhones(phones);
}

// Asks each phone to connect. That takes seconds, or fails when the phone is out of reach, so the answer is only logged
// when it comes; the phone then opens the Android Auto service by itself (service thread).
void BluetoothService::ConnectPhones(const std::vector<bluez::PairedDevice>& phones)
{
    auto bus = QDBusConnection::systemBus();
    for (const auto& device : phones) {
        m_context.Log(LogLevel::Info, "Asking the paired phone '" + device.name + "' to connect");
        const auto message = QDBusMessage::createMethodCall(QLatin1String(bluez::kService), device.path, QLatin1String(bluez::kDeviceInterface), QStringLiteral("Connect"));
        auto* pCall = new QDBusPendingCallWatcher(bus.asyncCall(message, 30000), m_worker.get());
        QObject::connect(pCall, &QDBusPendingCallWatcher::finished, m_worker.get(), [this, name = device.name](QDBusPendingCallWatcher* pFinished) {
            if (pFinished->isError())
                m_context.Log(LogLevel::Info, "Paired phone '" + name + "' did not connect (" + Text(pFinished->error().name()) + ": " + Text(pFinished->error().message()) +
                    "); it can still connect by itself");
            else
                m_context.Log(LogLevel::Info, "Paired phone '" + name + "' connected");
            pFinished->deleteLater();
        });
    }
}
}
