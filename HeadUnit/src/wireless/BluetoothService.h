#pragma once
#include "logging/Logger.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace headunit {
// Makes this computer a Bluetooth "car" for wireless Android Auto: visible under a name (HEATUNIT), pairable like a car
// (the phone shows a code, the head unit confirms it by itself), and offering the Android Auto Wireless service that
// the phone connects to. Talks to BlueZ over D-Bus. Bluetooth is switched on as needed: the adapter is powered, and an
// rfkill block (Raspberry Pi OS keeps Bluetooth blocked until someone switches it on) is lifted first.
//
// Everything BlueZ asks (pairing, connections) is answered on a thread of the service's own, so pairing works at any
// time, also while a session runs or the caller is busy. It needs a QCoreApplication (D-Bus); the application's event
// loop does not have to run. The public functions may be called from any thread except that one.
class BluetoothService {
public:
    // `onEvent` receives what the person at the head unit should see (the pairing code, a phone paired), in German. It
    // is called on the service's thread.
    explicit BluetoothService(Logger& logger, std::function<void(const std::string&)> onEvent = {});
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    // Returns when Bluetooth is visible. Empty on success, otherwise what went wrong and what to do about it (German,
    // for the window).
    std::string Start(const std::string& name);
    void Stop();
    bool IsRunning() const;
    // Asks the phones paired before to connect, as a car does when it starts, and returns at once. A phone that is
    // connected already (PipeWire connects calls and audio by itself, also before HeadUnit runs) is disconnected first:
    // Android Auto looks for its service when the connection starts, so a phone connected before the service existed may
    // not look again while that connection lasts. Only devices BlueZ shows as phones. Call it once the head unit is ready
    // for the phone (Wi-Fi up).
    void ConnectPairedPhones();
    // Waits up to `timeout` for a phone that opened the Android Auto Wireless service. Returns its connected
    // RFCOMM socket, which the caller owns and closes, or -1.
    int WaitForPhone(std::chrono::milliseconds timeout);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
