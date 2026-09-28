#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Wireless Android Auto, first half: before the phone joins the Wi-Fi it talks to the head unit over a Bluetooth RFCOMM
// connection. This file, WirelessFrameParser and WirelessHandshake are the message framing and the conversation on
// that link; they know nothing about Bluetooth or sockets, so they are unit-tested without a phone.
//
//   head unit -> phone   WifiStartRequest     "connect to <ip>:<port>"
//   phone -> head unit   WifiInfoRequest      "which network?"
//   head unit -> phone   WifiInfoResponse     SSID, password, BSSID, security mode
//   phone -> head unit   WifiStartResponse    status (and later WifiConnectionStatus)
// The phone then joins the network and opens a TCP connection to the port; from there on it is the same Android Auto
// session as over USB.
namespace headunit {
inline constexpr std::uint16_t kWirelessPort = 5288;
// The Bluetooth service the phone looks for on a car.
inline constexpr const char* kAndroidAutoWirelessUuid = "4de17a00-52cb-11e6-bdf4-0800200c9a66";
// Its RFCOMM channel, the first of these that is free. BlueZ opens an RFCOMM server for a service only when it is given
// a channel (or knows the UUID, which it does not for this one). The phone finds the channel in the service record, so
// any free one does. Not 8, which the wireless dongles use: BlueZ's SIM Access plugin (on in Raspberry Pi OS) holds it,
// and BlueZ's own profiles, PipeWire and OBEX use 1 to 17. A taken channel does not make the registration fail; BlueZ
// then just publishes nothing (see BluetoothService, which checks).
inline constexpr std::uint16_t kAndroidAutoWirelessChannels[] = {22, 23, 24, 25, 26, 27, 28, 29, 30};

// The security mode as the phone reads it in the Wi-Fi details. Android numbers the modes as bit flags (WPA 4, WPA2 8,
// both 12, enterprise +16); the imported enum WifiSecurityMode numbers them 0 to 9 instead, so its WPA2_PERSONAL (5) is
// a value the phone does not know, and the phone then never joins the network. 8 is what the implementations that work
// with real phones send (openauto's Bluetooth service, WirelessAndroidAutoDongle).
inline constexpr int kWifiSecurityWpa2Personal = 8;

// Message ids of aap_protobuf.aaw.MessageId.
enum class WirelessMessageId : std::uint16_t {
    StartRequest = 1, InfoRequest = 2, InfoResponse = 3, VersionRequest = 4, VersionResponse = 5, ConnectionStatus = 6, StartResponse = 7,
};

// The Wi-Fi the phone is told to join, and where the head unit listens in it.
struct WifiCredentials {
    std::string ssid;
    std::string password;
    std::string bssid;       // the access point's MAC address, "AA:BB:CC:DD:EE:FF"
    std::string ipAddress;   // the head unit's address in that network
    std::uint16_t port{kWirelessPort};
};

// One message of the Bluetooth conversation.
struct WirelessMessage {
    std::uint16_t id{};
    std::string payload;     // the serialized protobuf message
};

std::vector<std::uint8_t> EncodeWirelessMessage(const WirelessMessage& message);
}
