#pragma once
#include <QDBusMessage>
#include <QObject>

namespace headunit {
class BluetoothContext;

// Follows what the phones do to their Bluetooth link (BlueZ's PropertiesChanged signals): whether a phone pairs and
// connects at all is the first thing to know when nothing happens. A paired phone is trusted and told about on the
// window; a phone that connects from now on finds the Android Auto service.
class DeviceWatcher : public QObject {
    Q_OBJECT

public:
    explicit DeviceWatcher(BluetoothContext& context);

public slots:
    void OnPropertiesChanged(const QDBusMessage& message);

private:
    BluetoothContext& m_context;
};
}
