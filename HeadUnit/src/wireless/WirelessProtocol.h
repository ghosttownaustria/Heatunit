#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace headunit {
// Wireless Android Auto, first half: before the phone joins the Wi-Fi it talks to the head unit over a Bluetooth
// RFCOMM connection. This file is the message framing and the conversation on that link; it knows nothing about
// Bluetooth or sockets, so it is unit-tested without a phone.
//
//   head unit -> phone   WifiStartRequest     "connect to <ip>:<port>"
//   phone -> head unit   WifiInfoRequest      "which network?"
//   head unit -> phone   WifiInfoResponse     SSID, password, BSSID, security mode
//   phone -> head unit   WifiStartResponse    status (and later WifiConnectionStatus)
// The phone then joins the network and opens a TCP connection to the port; from there on it is the same
// Android Auto session as over USB.

constexpr std::uint16_t kWirelessPort = 5288;
// The Bluetooth service the phone looks for on a car.
constexpr const char* kAndroidAutoWirelessUuid = "4de17a00-52cb-11e6-bdf4-0800200c9a66";

// The security mode as the phone reads it in the Wi-Fi details. Android numbers the modes as bit flags (WPA 4, WPA2 8,
// both 12, enterprise +16); the imported enum WifiSecurityMode numbers them 0 to 9 instead, so its WPA2_PERSONAL (5) is
// a value the phone does not know, and the phone then never joins the network. 8 is what the implementations that work
// with real phones send (openauto's Bluetooth service, WirelessAndroidAutoDongle).
constexpr int kWifiSecurityWpa2Personal = 8;

// Message ids of aap_protobuf.aaw.MessageId.
enum class WirelessMessageId : std::uint16_t {
    StartRequest = 1, InfoRequest = 2, InfoResponse = 3, VersionRequest = 4, VersionResponse = 5, ConnectionStatus = 6, StartResponse = 7,
};

struct WifiCredentials {
    std::string ssid;
    std::string password;
    std::string bssid;       // the access point's MAC address, "AA:BB:CC:DD:EE:FF"
    std::string ipAddress;   // the head unit's address in that network
    std::uint16_t port{kWirelessPort};
};

struct WirelessMessage {
    std::uint16_t id{};
    std::string payload;     // the serialized protobuf message
};

// On the wire: 2 bytes payload size, 2 bytes message id (both big endian), then the payload.
std::vector<std::uint8_t> EncodeWirelessMessage(const WirelessMessage& message);

// Bytes in, complete messages out: a message may arrive in pieces, and several may arrive at once.
class WirelessFrameParser {
public:
    std::vector<WirelessMessage> Feed(const std::uint8_t* data, std::size_t size);
private:
    std::vector<std::uint8_t> m_buffer;
};

struct WirelessHandshakeStep {
    std::vector<WirelessMessage> reply;   // to send to the phone
    std::string note;                     // what happened, for the log
};

class WirelessHandshake {
public:
    explicit WirelessHandshake(WifiCredentials credentials);
    // What the head unit says first, right after the phone connected.
    WirelessHandshakeStep Start();
    WirelessHandshakeStep OnMessage(const WirelessMessage& message);
    // The phone reported that it cannot use the network (wrong password, Wi-Fi off, no suitable channel, ...).
    bool IsFailed() const { return m_isFailed; }
    const std::string& Failure() const { return m_failure; }
    bool HasSentInfo() const { return m_hasSentInfo; }
private:
    WifiCredentials m_credentials;
    bool m_isFailed{};
    bool m_hasSentInfo{};
    std::string m_failure;
};
}
