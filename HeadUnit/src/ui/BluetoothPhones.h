#pragma once
#include <string>
#include <vector>

namespace headunit {
// A phone paired with the head unit over Bluetooth, as the Bluetooth page lists it. Filled by the Bluetooth service
// (Linux only); the page and its rules are portable.
struct BluetoothPhone {
    friend bool operator==(const BluetoothPhone&, const BluetoothPhone&) = default;

    std::string id;          // BlueZ's device path, how the service finds the phone again
    std::string name;        // the name the phone gave itself ("Jakob's Flip 8")
    bool isConnected{};
    bool isAndroidAuto{};    // the phone of the running wireless Android Auto session
    bool isAutoConnect{true};   // the head unit connects it by itself; false: only the Connect button starts Android Auto on it
};

std::vector<BluetoothPhone> SortedPhones(std::vector<BluetoothPhone> phones);
std::string PhoneStateText(const BluetoothPhone& phone);
bool CanSwitchTo(const BluetoothPhone& phone);
}
