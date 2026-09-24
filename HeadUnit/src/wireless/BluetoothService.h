#pragma once
#include "logging/Logger.h"
#include <chrono>
#include <memory>
#include <string>

namespace headunit {
// Makes this computer a Bluetooth "car" for wireless Android Auto: visible under a name (HEATUNIT), pairable without
// a PIN, and offering the Android Auto Wireless service that the phone connects to. Talks to BlueZ over D-Bus.
// Bluetooth is switched on as needed: the adapter is powered, and an rfkill block (Raspberry Pi OS keeps Bluetooth
// blocked until someone switches it on) is lifted first.
//
// Every call must come from the same thread, and that thread has to be able to run Qt events (it needs a
// QCoreApplication): the D-Bus calls from BlueZ are delivered only while WaitForPhone() or Pump() runs.
class BluetoothService {
public:
    explicit BluetoothService(Logger& logger);
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    // Empty on success, otherwise what went wrong and what to do about it (German, for the window).
    std::string Start(const std::string& name);
    void Stop();
    // Asks the phones paired before to connect, as a car does when it starts. A phone that is connected already (PipeWire
    // connects calls and audio by itself, also while HeadUnit is not in wireless mode) is disconnected first: Android
    // Auto looks for its service when the connection starts, so a phone connected before the service existed may not
    // look again while that connection lasts. Only devices BlueZ shows as phones. Call it after Start(), once the head
    // unit is ready for the phone.
    void ConnectPairedPhones();
    // Waits up to `timeout` for a phone that opened the Android Auto Wireless service. Returns its connected
    // RFCOMM socket, which the caller owns and closes, or -1.
    int WaitForPhone(std::chrono::milliseconds timeout);
    // Answers BlueZ's calls for `duration` (pairing, connections), for a caller that waits for something else.
    void Pump(std::chrono::milliseconds duration);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
