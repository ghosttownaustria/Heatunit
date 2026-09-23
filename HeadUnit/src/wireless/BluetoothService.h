#pragma once
#include "logging/Logger.h"
#include <chrono>
#include <memory>
#include <string>

namespace headunit {
// Makes this computer a Bluetooth "car" for wireless Android Auto: visible under a name (HEATUNIT), pairable without
// a PIN, and offering the Android Auto Wireless service that the phone connects to. Talks to BlueZ over D-Bus.
//
// Every call must come from the same thread, and that thread has to be able to run Qt events (it needs a
// QCoreApplication): the D-Bus calls from BlueZ are delivered while WaitForPhone() waits.
class BluetoothService {
public:
    explicit BluetoothService(Logger& logger);
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    // Empty on success, otherwise what went wrong and what to do about it (German, for the window).
    std::string Start(const std::string& name);
    void Stop();
    // Waits up to `timeout` for a phone that opened the Android Auto Wireless service. Returns its connected
    // RFCOMM socket, which the caller owns and closes, or -1.
    int WaitForPhone(std::chrono::milliseconds timeout);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
