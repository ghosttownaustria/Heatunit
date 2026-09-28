#include "wireless/PairingAgent.h"
#include "wireless/BluetoothContext.h"
#include "wireless/BluezCalls.h"
#include <cstdio>
#include <string>

namespace headunit {
namespace {
// The passkey as the six digits both sides show.
std::string PasskeyText(quint32 passkey)
{
    char text[16]{};
    std::snprintf(text, sizeof(text), "%06u", static_cast<unsigned>(passkey));
    return text;
}
}

// An agent on `parent` (the D-Bus object it is registered under), which owns it.
PairingAgent::PairingAgent(QObject* parent, BluetoothContext& context) : QDBusAbstractAdaptor(parent), m_context(context)
{
}

// BlueZ no longer uses the agent.
void PairingAgent::Release()
{
    m_context.Log(LogLevel::Info, "Pairing agent released by BlueZ");
}

// Only phones without Secure Simple Pairing (none from the last decade) ask for a PIN.
QString PairingAgent::RequestPinCode(const QDBusObjectPath& device)
{
    m_context.Log(LogLevel::Info, "PIN requested by " + bluez::Text(device.path()) + "; answering 0000");
    return QStringLiteral("0000");
}

// A PIN to show; only logged.
void PairingAgent::DisplayPinCode(const QDBusObjectPath& device, const QString& pin)
{
    m_context.Log(LogLevel::Info, "PIN for " + bluez::Text(device.path()) + ": " + bluez::Text(pin));
}

// A passkey to type in on this side, which has no keyboard.
quint32 PairingAgent::RequestPasskey(const QDBusObjectPath& device)
{
    m_context.Log(LogLevel::Info, "Passkey requested by " + bluez::Text(device.path()) + "; answering 0");
    return 0;
}

// A passkey the phone's user types in.
void PairingAgent::DisplayPasskey(const QDBusObjectPath& device, quint32 passkey, quint16 /*entered*/)
{
    const auto code = PasskeyText(passkey);
    m_context.Log(LogLevel::Info, "Passkey for " + bluez::Text(device.path()) + ": " + code);
    m_context.Tell("Bluetooth-Kopplung: am Handy den Code " + code + " eingeben.");
}

// The code comparison: the reply waits for the person's answer on the head unit's screen, where there is one.
void PairingAgent::RequestConfirmation(const QDBusObjectPath& device, quint32 passkey, const QDBusMessage& message)
{
    const auto name = bluez::DeviceName(device.path());
    const auto code = PasskeyText(passkey);
    m_context.Log(LogLevel::Info, "Pairing request from '" + name + "' (" + bluez::Text(device.path()) + "), code " + code);
    if (m_context.CanAskForPairing()) {
        m_context.AskForPairing(message, device.path(), name, code);
        return;
    }
    m_context.Log(LogLevel::Info, "Pairing with '" + name + "' confirmed by itself");
    m_context.Tell("Bluetooth-Kopplung mit " + name + ": am Handy den Code " + code + " bestaetigen.");
    bluez::Trust(device.path());
}

// A pairing without a code (Just Works) or a connection from a device BlueZ does not trust yet.
void PairingAgent::RequestAuthorization(const QDBusObjectPath& device)
{
    m_context.Log(LogLevel::Info, "Pairing authorized for " + bluez::Text(device.path()));
    bluez::Trust(device.path());
}

// A device that is not trusted yet wants to use a service.
void PairingAgent::AuthorizeService(const QDBusObjectPath& device, const QString& uuid)
{
    m_context.Log(LogLevel::Info, "Service " + bluez::Text(uuid) + " authorized for " + bluez::Text(device.path()));
    bluez::Trust(device.path());
}

// The phone gave up, or BlueZ stopped waiting for the answer.
void PairingAgent::Cancel()
{
    m_context.Log(LogLevel::Info, "Pairing request cancelled");
    m_context.EndPairing(false);
}
}
