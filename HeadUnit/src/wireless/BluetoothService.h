#pragma once
#include "logging/Logger.h"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace headunit {
// A phone asks to pair. It shows `code` and asks its user to confirm; a car shows the same code and asks too.
struct PairingRequest {
    std::string phone;   // the name the phone gave itself
    std::string code;    // six digits
    // true pairs, false refuses. Any thread, at most once; after the request has ended it does nothing.
    std::function<void(bool isAccepted)> answer;
};

// What the service tells the head unit. All of it is called on the service's thread.
struct BluetoothEvents {
    // What the person at the head unit should see (a phone paired, connected), in German.
    std::function<void(const std::string&)> onStatus;
    // A pairing to confirm. Without it every pairing is confirmed at once.
    std::function<void(const PairingRequest&)> onPairingRequest;
    // The pairing request has ended (answered, cancelled by the phone, timed out): the question can go.
    std::function<void()> onPairingEnd;
};

// Makes this computer a Bluetooth "car" for wireless Android Auto: visible under a name (HEATUNIT), pairable like a car
// (phone and head unit show the same code; see BluetoothEvents), and offering the Android Auto Wireless service that
// the phone connects to. Talks to BlueZ over D-Bus. Bluetooth is switched on as needed: the adapter is powered, and an
// rfkill block (Raspberry Pi OS keeps Bluetooth blocked until someone switches it on) is lifted first.
//
// Everything BlueZ asks (pairing, connections) is answered on a thread of the service's own, so pairing works at any
// time, also while a session runs or the caller is busy. It needs a QCoreApplication (D-Bus); the application's event
// loop does not have to run. The public functions may be called from any thread except that one.
class BluetoothService {
public:
    explicit BluetoothService(Logger& logger, BluetoothEvents events = {});
    ~BluetoothService();
    BluetoothService(const BluetoothService&) = delete;
    BluetoothService& operator=(const BluetoothService&) = delete;

    // Returns when Bluetooth is visible. Empty on success, otherwise what went wrong and what to do about it (German,
    // for the window).
    std::string Start(const std::string& name);
    void Stop();
    bool IsRunning() const;
    // Asks the phones paired before to connect, as a car does when it starts, and returns at once. Android Auto looks
    // for its service when the Bluetooth connection starts, so a phone that is connected already is disconnected first:
    // with `isReconnectingAll` every connected phone (the person asked for Android Auto), otherwise only those that were
    // connected before the service existed (PipeWire connects calls and audio by itself, also before HeadUnit runs).
    // Only devices BlueZ shows as phones. Call it once the head unit is ready for the phone (Wi-Fi up).
    void ConnectPairedPhones(bool isReconnectingAll = false);
    // Waits up to `timeout` for a phone that opened the Android Auto Wireless service. Returns its connected
    // RFCOMM socket, which the caller owns and closes, or -1.
    int WaitForPhone(std::chrono::milliseconds timeout);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
