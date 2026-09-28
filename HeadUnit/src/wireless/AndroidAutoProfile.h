#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QDBusUnixFileDescriptor>
#include <QVariantMap>

namespace headunit {
class BluetoothContext;

// The Android Auto Wireless service (org.bluez.Profile1). BlueZ hands over the connected socket of every phone that
// opens it; the socket goes to BluetoothContext::AddPhone. The slot names are BlueZ's method names.
class AndroidAutoProfile : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Profile1")

public:
    AndroidAutoProfile(QObject* parent, BluetoothContext& context);

public slots:
    void Release();
    void NewConnection(const QDBusObjectPath& device, const QDBusUnixFileDescriptor& fd, const QVariantMap& properties);
    void RequestDisconnection(const QDBusObjectPath& device);

private:
    BluetoothContext& m_context;
};
}
