#pragma once
#include <functional>
#include <string>

namespace headunit {
// A phone asks to pair. It shows `code` and asks its user to confirm; a car shows the same code and asks too.
struct PairingRequest {
    std::string phone;   // the name the phone gave itself
    std::string code;    // six digits
    // true pairs, false refuses. Any thread, at most once; after the request has ended it does nothing.
    std::function<void(bool isAccepted)> answer;
};

// What the Bluetooth service tells the head unit. All of it is called on the service's thread.
struct BluetoothEvents {
    // What the person at the head unit should see (a phone paired, connected), in German.
    std::function<void(const std::string&)> onStatus;
    // A pairing to confirm. Without it every pairing is confirmed at once.
    std::function<void(const PairingRequest&)> onPairingRequest;
    // The pairing request has ended (answered, cancelled by the phone, timed out): the question can go.
    std::function<void()> onPairingEnd;
};
}
