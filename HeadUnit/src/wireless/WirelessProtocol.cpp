#include "wireless/WirelessProtocol.h"
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <stdexcept>
#include <utility>

namespace headunit {
namespace aaw = aap_protobuf::aaw;
namespace {
constexpr std::size_t kHeaderSize = 4;
WirelessMessage Make(WirelessMessageId id, std::string payload) { return {static_cast<std::uint16_t>(id), std::move(payload)}; }
std::string StatusText(int status) { return std::to_string(status); }
}

std::vector<std::uint8_t> EncodeWirelessMessage(const WirelessMessage& message)
{
    if (message.payload.size() > 0xFFFF) throw std::length_error("Wireless message payload is too large");
    const auto size = static_cast<std::uint16_t>(message.payload.size());
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderSize + message.payload.size());
    bytes.push_back(static_cast<std::uint8_t>(size >> 8));
    bytes.push_back(static_cast<std::uint8_t>(size & 0xFF));
    bytes.push_back(static_cast<std::uint8_t>(message.id >> 8));
    bytes.push_back(static_cast<std::uint8_t>(message.id & 0xFF));
    bytes.insert(bytes.end(), message.payload.begin(), message.payload.end());
    return bytes;
}

std::vector<WirelessMessage> WirelessFrameParser::Feed(const std::uint8_t* data, std::size_t size)
{
    m_buffer.insert(m_buffer.end(), data, data + size);
    std::vector<WirelessMessage> messages;
    std::size_t offset = 0;
    while (m_buffer.size() - offset >= kHeaderSize) {
        const std::size_t payloadSize = (static_cast<std::size_t>(m_buffer[offset]) << 8) | m_buffer[offset + 1];
        if (m_buffer.size() - offset < kHeaderSize + payloadSize) break;
        WirelessMessage message;
        message.id = static_cast<std::uint16_t>((m_buffer[offset + 2] << 8) | m_buffer[offset + 3]);
        const auto begin = m_buffer.begin() + static_cast<std::ptrdiff_t>(offset + kHeaderSize);
        message.payload.assign(begin, begin + static_cast<std::ptrdiff_t>(payloadSize));
        messages.push_back(std::move(message));
        offset += kHeaderSize + payloadSize;
    }
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + static_cast<std::ptrdiff_t>(offset));
    return messages;
}

WirelessHandshake::WirelessHandshake(WifiCredentials credentials) : m_credentials(std::move(credentials)) {}

WirelessHandshakeStep WirelessHandshake::Start()
{
    aaw::WifiStartRequest request;
    request.set_ip_address(m_credentials.ipAddress);
    request.set_port(m_credentials.port);
    return {{Make(WirelessMessageId::StartRequest, request.SerializeAsString())},
        "Sent the start request: the phone should connect to " + m_credentials.ipAddress + ":" + std::to_string(m_credentials.port)};
}

WirelessHandshakeStep WirelessHandshake::OnMessage(const WirelessMessage& message)
{
    switch (static_cast<WirelessMessageId>(message.id)) {
    case WirelessMessageId::InfoRequest: {
        namespace wifi = aap_protobuf::service::wifiprojection::message;
        aaw::WifiInfoResponse response;
        response.set_ssid(m_credentials.ssid);
        response.set_password(m_credentials.password);
        response.set_bssid(m_credentials.bssid);
        response.set_security_mode(wifi::WPA2_PERSONAL);
        response.set_access_point_type(wifi::DYNAMIC);
        m_hasSentInfo = true;
        return {{Make(WirelessMessageId::InfoResponse, response.SerializeAsString())},
            "The phone asked for the Wi-Fi details; sent network '" + m_credentials.ssid + "' (BSSID " + m_credentials.bssid + ")"};
    }
    case WirelessMessageId::StartResponse: {
        aaw::WifiStartResponse response;
        if (!response.ParseFromString(message.payload)) return {{}, "The phone's start response could not be read"};
        if (response.status() < 0) {
            m_isFailed = true;
            m_failure = "The phone could not start the wireless connection (status " + StatusText(response.status()) + ")";
            return {{}, m_failure};
        }
        return {{}, "The phone answered the start request (status " + StatusText(response.status()) + ")"};
    }
    case WirelessMessageId::ConnectionStatus: {
        aaw::WifiConnectionStatus status;
        if (!status.ParseFromString(message.payload)) return {{}, "The phone's connection status could not be read"};
        if (status.status() < 0) {
            m_isFailed = true;
            m_failure = "The phone could not join the Wi-Fi (status " + StatusText(status.status()) +
                (status.has_error_message() ? ": " + status.error_message() : std::string()) + ")";
            return {{}, m_failure};
        }
        return {{}, "The phone reports its Wi-Fi connection (status " + StatusText(status.status()) + ")"};
    }
    default:
        return {{}, "Ignored a message from the phone: id " + std::to_string(message.id) + ", " + std::to_string(message.payload.size()) + " bytes"};
    }
}
}
