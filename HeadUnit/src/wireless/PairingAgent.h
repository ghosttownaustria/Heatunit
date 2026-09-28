#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QString>

namespace headunit {
class BluetoothContext;

// BlueZ's pairing agent (org.bluez.Agent1). Pairing as in a car: BlueZ is told that this side has a display and a yes/no
// button ("DisplayYesNo"), so the phone and the head unit show the same six-digit code (numeric comparison) and both are
// confirmed: on the phone, and on the head unit's screen (BluetoothEvents::onPairingRequest; without a screen the head
// unit confirms by itself). Just Works pairing (no code, "NoInputNoOutput") is not used: BlueZ refuses it for a phone it
// has paired before (JustWorksRepairing = never, its default), so a phone that forgot the pairing and pairs again got
// "pairing not done". A code comparison is not affected by that rule. The slot names are BlueZ's method names.
class PairingAgent : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    PairingAgent(QObject* parent, BluetoothContext& context);

public slots:
    void Release();
    QString RequestPinCode(const QDBusObjectPath& device);
    void DisplayPinCode(const QDBusObjectPath& device, const QString& pin);
    quint32 RequestPasskey(const QDBusObjectPath& device);
    void DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 entered);
    void RequestConfirmation(const QDBusObjectPath& device, quint32 passkey, const QDBusMessage& message);
    void RequestAuthorization(const QDBusObjectPath& device);
    void AuthorizeService(const QDBusObjectPath& device, const QString& uuid);
    void Cancel();

private:
    BluetoothContext& m_context;
};
}
