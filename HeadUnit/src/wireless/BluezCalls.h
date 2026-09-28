#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QList>
#include <QString>
#include <QVariant>
#include <optional>
#include <string>
#include <vector>

namespace headunit {
class BluetoothContext;

// Calls to BlueZ (org.bluez on the system bus) and what the service reads from it.
namespace bluez {
inline constexpr const char* kService = "org.bluez";
inline constexpr const char* kAdapterInterface = "org.bluez.Adapter1";
inline constexpr const char* kDeviceInterface = "org.bluez.Device1";
inline constexpr const char* kPropertiesInterface = "org.freedesktop.DBus.Properties";

// A device BlueZ has paired.
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

std::string Text(const QString& value);
std::string ErrorText(const QDBusMessage& reply);
QDBusMessage CallRaw(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* method, const QList<QVariant>& arguments);
std::string Call(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* method, const QList<QVariant>& arguments);
std::optional<QVariant> GetProperty(QDBusConnection& bus, const QString& path, const char* interfaceName, const char* property);
std::string SetAdapterProperty(QDBusConnection& bus, const QString& adapter, const char* property, const QVariant& value);
bool IsServiceListed(QDBusConnection& bus, const QString& adapter, const char* uuid);
std::string DeviceName(const QString& devicePath);
void Trust(const QString& devicePath);
BluezState ReadBluez(QDBusConnection& bus);
std::string PowerOn(QDBusConnection& bus, const QString& adapter, const BluetoothContext& context);
}
}
