#include "wireless/BluezCalls.h"
#include "wireless/BluetoothContext.h"
#include "wireless/SystemCommand.h"
#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <linux/rfkill.h>
#include <thread>
#include <unistd.h>

namespace headunit::bluez {
namespace {
using namespace std::chrono_literals;

// The rfkill switches of the Bluetooth adapters.
struct RfkillState {
    bool isPresent{};
    bool isSoftBlocked{};
    bool isHardBlocked{};
};

// One line of a sysfs attribute file.
std::string ReadSysfsValue(const std::filesystem::path& path)
{
    std::ifstream file(path);
    std::string value;
    std::getline(file, value);
    return value;
}

// Whether Bluetooth is switched off by rfkill, in software or with a hardware switch.
RfkillState ReadBluetoothRfkill()
{
    RfkillState state;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator("/sys/class/rfkill", error)) {
        if (ReadSysfsValue(entry.path() / "type") != "bluetooth") continue;
        state.isPresent = true;
        state.isSoftBlocked |= ReadSysfsValue(entry.path() / "soft") == "1";
        state.isHardBlocked |= ReadSysfsValue(entry.path() / "hard") == "1";
    }
    return state;
}

// The rfkill state for the log.
std::string RfkillText(const RfkillState& rfkill)
{
    if (!rfkill.isPresent) return "no entry";
    if (rfkill.isHardBlocked) return "hard blocked";
    return rfkill.isSoftBlocked ? "soft blocked" : "not blocked";
}

// What `rfkill unblock bluetooth` does. /dev/rfkill is writable for the user logged in at the screen (systemd's uaccess
// rule); over SSH it usually is not, and then sudo without a password (Raspberry Pi OS's first user has it) is tried.
bool UnblockBluetooth(const BluetoothContext& context)
{
    if (const int fd = ::open("/dev/rfkill", O_RDWR | O_CLOEXEC); fd >= 0) {
        rfkill_event event{};
        event.type = RFKILL_TYPE_BLUETOOTH;
        event.op = RFKILL_OP_CHANGE_ALL;
        event.soft = 0;
        const auto written = ::write(fd, &event, sizeof(event));
        const int writeError = errno;
        ::close(fd);
        if (written == static_cast<ssize_t>(sizeof(event))) {
            context.Log(LogLevel::Info, "Bluetooth unblocked through /dev/rfkill");
            return true;
        }
        context.Log(LogLevel::Info, std::string("/dev/rfkill is not writable: ") + std::strerror(writeError));
    } else {
        context.Log(LogLevel::Info, std::string("/dev/rfkill cannot be opened: ") + std::strerror(errno));
    }
    const auto command = RunCommand({"sudo", "-n", "rfkill", "unblock", "bluetooth"}, 10s);
    if (command.exitCode == 0) {
        context.Log(LogLevel::Info, "Bluetooth unblocked with sudo rfkill");
        return true;
    }
    context.Log(LogLevel::Warning, "sudo rfkill unblock bluetooth failed: " + (command.isStarted ? command.output : std::string("sudo not available")));
    return false;
}

// Whether the adapter is switched on.
bool IsPowered(QDBusConnection& bus, const QString& adapter)
{
    const auto value = GetProperty(bus, adapter, kAdapterInterface, "Powered");
    return value && value->toBool();
}

// Switches the adapter on; empty when it is on afterwards, otherwise BlueZ's error.
std::string SwitchOn(QDBusConnection& bus, const QString& adapter)
{
    auto error = SetAdapterProperty(bus, adapter, "Powered", true);
    if (error.empty() && !IsPowered(bus, adapter)) error = "stays off";
    return error;
}

// The paired devices and the adapters in BlueZ's object list (a{oa{sa{sv}}}), adapters sorted so that hci0 comes first.
void ReadManagedObjects(const QDBusArgument& objects, BluezState& state)
{
    std::vector<QString> adapters;
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
            if (interfaceName == QLatin1String(kAdapterInterface)) {
                adapters.push_back(path.path());
            } else if (interfaceName == QLatin1String(kDeviceInterface) && properties.value(QStringLiteral("Paired")).toBool()) {
                state.paired.push_back({path.path(), Text(properties.value(QStringLiteral("Alias")).toString()),
                    properties.value(QStringLiteral("Connected")).toBool(), properties.value(QStringLiteral("Icon")).toString() == QLatin1String("phone")});
            }
        }
        objects.endMap();
        objects.endMapEntry();
    }
    objects.endMap();
    std::sort(adapters.begin(), adapters.end());
    if (!adapters.empty()) state.adapterPath = adapters.front();
}
}

// A Qt string as UTF-8.
std::string Text(const QString& value)
{
    return value.toStdString();
}

// BlueZ's error for the log and the window.
std::string ErrorText(const QDBusMessage& reply)
{
    return Text(reply.errorName()) + ": " + Text(reply.errorMessage());
}

// Calls `method` of BlueZ's object at `path` and waits up to eight seconds for the reply.
QDBusMessage CallRaw(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* method, const QList<QVariant>& arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(QLatin1String(kService), path, QLatin1String(interfaceName), QLatin1String(method));
    message.setArguments(arguments);
    return bus.call(message, QDBus::Block, 8000);
}

// Calls `method` like CallRaw; empty when the call worked, otherwise BlueZ's error.
std::string Call(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* method, const QList<QVariant>& arguments)
{
    const auto reply = CallRaw(bus, path, interfaceName, method, arguments);
    return reply.type() == QDBusMessage::ErrorMessage ? ErrorText(reply) : std::string();
}

// A property of BlueZ's object at `path`; nothing when BlueZ does not answer.
std::optional<QVariant> GetProperty(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* property)
{
    const auto reply = CallRaw(bus, path, kPropertiesInterface, "Get", {QString::fromLatin1(interfaceName), QString::fromLatin1(property)});
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) return std::nullopt;
    // Get answers with a variant, which QtDBus hands over wrapped in a QDBusVariant.
    const QVariant value = reply.arguments().at(0);
    if (value.userType() == qMetaTypeId<QDBusVariant>()) return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

// Sets a property of the adapter; empty when it worked, otherwise BlueZ's error.
std::string SetAdapterProperty(QDBusConnection& bus, const QString& adapter, const char* property, const QVariant& value)
{
    return Call(bus, adapter, kPropertiesInterface, "Set", {QString::fromLatin1(kAdapterInterface), QString::fromLatin1(property), QVariant::fromValue(QDBusVariant(value))});
}

// Whether the adapter publishes a service with this UUID (Adapter1.UUIDs, the local service records). BlueZ adds a
// registered profile's record there only once its RFCOMM server listens, so the list is read up to three times.
bool IsServiceListed(QDBusConnection& bus, const QString& adapter, const char* uuid)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (attempt > 0) std::this_thread::sleep_for(150ms);
        const auto uuids = GetProperty(bus, adapter, kAdapterInterface, "UUIDs");
        if (uuids && uuids->toStringList().contains(QString::fromLatin1(uuid), Qt::CaseInsensitive)) return true;
    }
    return false;
}

// The name the phone gave itself ("Galaxy Z Flip5"), for the window; the device path when BlueZ does not answer.
std::string DeviceName(const QString& devicePath)
{
    auto bus = QDBusConnection::systemBus();
    const auto name = GetProperty(bus, devicePath, kDeviceInterface, "Alias");
    return name && !name->toString().isEmpty() ? Text(name->toString()) : Text(devicePath);
}

// Marks a device as trusted: BlueZ then lets it reconnect and use its services without asking this program again
// (those questions would wait unanswered while a session runs). The answer is not needed.
void Trust(const QString& devicePath)
{
    auto message = QDBusMessage::createMethodCall(QLatin1String(kService), devicePath, QLatin1String(kPropertiesInterface), QStringLiteral("Set"));
    message.setArguments({QString::fromLatin1(kDeviceInterface), QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))});
    QDBusConnection::systemBus().send(message);
}

// Whether BlueZ runs, its first adapter, and the devices paired so far.
BluezState ReadBluez(QDBusConnection& bus)
{
    BluezState state;
    const auto reply = CallRaw(bus, QStringLiteral("/"), "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", {});
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        state.error = ErrorText(reply);
        return state;
    }
    state.isRunning = true;
    ReadManagedObjects(qvariant_cast<QDBusArgument>(reply.arguments().at(0)), state);
    return state;
}

// Switches the adapter on, lifting an rfkill block first where there is one. Empty when the adapter is on, otherwise
// what went wrong and what to do about it (German, for the window).
std::string PowerOn(QDBusConnection& bus, const QString& adapter, const BluetoothContext& context)
{
    if (IsPowered(bus, adapter)) return {};
    auto error = SwitchOn(bus, adapter);
    if (error.empty()) {
        context.Log(LogLevel::Info, "Bluetooth adapter switched on");
        return {};
    }
    const auto rfkill = ReadBluetoothRfkill();
    context.Log(LogLevel::Info, "Bluetooth adapter is off (" + error + "); rfkill: " + RfkillText(rfkill));
    if (rfkill.isHardBlocked)
        return "Bluetooth ist per Hardware gesperrt (rfkill hard block) und laesst sich von hier nicht einschalten. Pruefen mit: rfkill list";
    if (rfkill.isSoftBlocked && !UnblockBluetooth(context))
        return "Bluetooth ist gesperrt (rfkill) und liess sich nicht entsperren. Einmalig im Terminal: sudo rfkill unblock bluetooth";
    // After the unblock the adapter needs a moment before it can be switched on.
    for (int attempt = 0; attempt < 25; ++attempt) {
        std::this_thread::sleep_for(200ms);
        error = SwitchOn(bus, adapter);
        if (error.empty()) {
            context.Log(LogLevel::Info, "Bluetooth adapter switched on");
            return {};
        }
    }
    return "Der Bluetooth-Adapter laesst sich nicht einschalten (" + error + "). Pruefen mit: bluetoothctl show und rfkill list";
}
}
