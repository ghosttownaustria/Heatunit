#include "wireless/DeviceWatcher.h"
#include "wireless/BluetoothContext.h"
#include "wireless/BluezCalls.h"
#include <QDBusArgument>
#include <QVariantMap>

namespace headunit {
// A watcher that reports to `context`.
DeviceWatcher::DeviceWatcher(BluetoothContext& context) : m_context(context)
{
}

// A device's properties changed: logs pairing and connection changes and tells the window about them.
void DeviceWatcher::OnPropertiesChanged(const QDBusMessage& message)
{
    const auto arguments = message.arguments();
    if (arguments.size() < 2 || arguments.at(0).toString() != QLatin1String(bluez::kDeviceInterface)) return;
    const QVariantMap changed = qdbus_cast<QVariantMap>(arguments.at(1));
    for (const char* key : {"Paired", "Connected", "Trusted"}) {
        if (changed.contains(QLatin1String(key)))
            m_context.Log(LogLevel::Info, "Device " + bluez::Text(message.path()) + ": " + key + " = " + bluez::Text(changed.value(QLatin1String(key)).toString()));
    }
    if (changed.value(QStringLiteral("Paired")).toBool()) {
        bluez::Trust(message.path());
        m_context.EndPairing(false);
        m_context.Tell("Bluetooth: " + bluez::DeviceName(message.path()) + " ist gekoppelt. Android Auto meldet sich jetzt am Handy.");
    }
    if (changed.contains(QStringLiteral("Connected"))) {
        m_context.ForgetConnectedBefore(message.path());
        if (changed.value(QStringLiteral("Connected")).toBool())
            m_context.Tell("Bluetooth: " + bluez::DeviceName(message.path()) + " ist verbunden. Warte, bis Android Auto am Handy den Dienst oeffnet ...");
    }
}
}
