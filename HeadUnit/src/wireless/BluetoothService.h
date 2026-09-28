#pragma once
#include "logging/Logger.h"
#include "wireless/BluetoothContext.h"
#include "wireless/BluetoothEvents.h"
#include "wireless/BluezCalls.h"
#include <QString>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QObject;
class QThread;

namespace headunit {
class DeviceWatcher;

// Makes this computer a Bluetooth "car" for wireless Android Auto: visible under a name (HEATUNIT), pairable like a car
// (phone and head unit show the same code; see PairingAgent), and offering the Android Auto Wireless service that the
// phone connects to. Talks to BlueZ over D-Bus. Bluetooth is switched on as needed: the adapter is powered, and an
// rfkill block (Raspberry Pi OS keeps Bluetooth blocked until someone switches it on) is lifted first.
//
// Everything BlueZ asks (pairing, connections) is answered on a thread of the service's own, so pairing works at any
// time, also while a session runs or the caller is busy. It needs a QCoreApplication (D-Bus); the application's event
// loop does not have to run. The public methods may be called from any thread except that one.
class BluetoothService {
public:
    explicit BluetoothService(Logger& logger, BluetoothEvents events = {});
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    std::string Start(const std::string& name);
    void Stop();
    bool IsRunning() const;
    void ConnectPairedPhones(bool isReconnectingAll = false);
    int WaitForPhone(std::chrono::milliseconds timeout);

private:
    BluetoothContext m_context;
    std::unique_ptr<QThread> m_thread;   // answers BlueZ
    std::unique_ptr<QObject> m_worker;   // lives on m_thread and carries the work handed to it
    std::atomic_bool m_isRunning{};
    // Only used on m_thread:
    QString m_adapterPath;
    std::unique_ptr<QObject> m_agentObject;     // carries the PairingAgent
    std::unique_ptr<QObject> m_profileObject;   // carries the AndroidAutoProfile
    std::unique_ptr<DeviceWatcher> m_watcher;

    void RunOnServiceThread(const std::function<void()>& work);
    std::string StartOnServiceThread(const std::string& name);
    std::string RegisterObjects(QDBusConnection& bus);
    void RegisterAgent(QDBusConnection& bus);
    std::string RegisterProfile(QDBusConnection& bus, quint16& channel, bool& isListed);
    void MakeVisible(QDBusConnection& bus);
    void StopOnServiceThread();
    void ConnectPairedOnServiceThread(bool isReconnectingAll);
    void ConnectPhones(const std::vector<bluez::PairedDevice>& phones);
};
}
