#include "wireless/AndroidAutoProfile.h"
#include "wireless/BluetoothContext.h"
#include "wireless/BluezCalls.h"
#include <fcntl.h>

namespace headunit {
// A service on `parent` (the D-Bus object it is registered under), which owns it.
AndroidAutoProfile::AndroidAutoProfile(QObject* parent, BluetoothContext& context) : QDBusAbstractAdaptor(parent), m_context(context)
{
}

// BlueZ no longer uses the service.
void AndroidAutoProfile::Release()
{
    m_context.Log(LogLevel::Info, "Android Auto service released by BlueZ");
}

// A phone opened the service: its socket waits for the station.
void AndroidAutoProfile::NewConnection(const QDBusObjectPath& device, const QDBusUnixFileDescriptor& fd, const QVariantMap& /*properties*/)
{
    // The descriptor object closes its own copy when the call ends, so keep a duplicate.
    const int ownFd = fd.isValid() ? ::fcntl(fd.fileDescriptor(), F_DUPFD_CLOEXEC, 0) : -1;
    if (ownFd < 0) {
        m_context.Log(LogLevel::Error, "Connection from " + bluez::Text(device.path()) + " without a usable socket");
        return;
    }
    m_context.Log(LogLevel::Info, "Phone " + bluez::Text(device.path()) + " opened the Android Auto Wireless service");
    bluez::Trust(device.path());
    m_context.AddPhone(ownFd);
}

// The phone closes its connection to the service.
void AndroidAutoProfile::RequestDisconnection(const QDBusObjectPath& device)
{
    m_context.Log(LogLevel::Info, "Phone " + bluez::Text(device.path()) + " disconnected from the Android Auto service");
}
}
