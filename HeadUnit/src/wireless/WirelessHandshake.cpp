#include "wireless/WirelessHandshake.h"
#include <aap_protobuf/aaw/Status.pb.h>
#include <aap_protobuf/aaw/WifiConnectionStatus.pb.h>
#include <aap_protobuf/aaw/WifiInfoResponse.pb.h>
#include <aap_protobuf/aaw/WifiStartRequest.pb.h>
#include <aap_protobuf/aaw/WifiStartResponse.pb.h>
#include <utility>

namespace headunit {
namespace aaw = aap_protobuf::aaw;
namespace {
// A message of `id` with `payload`.
WirelessMessage MakeMessage(WirelessMessageId id, std::string payload)
{
    return {static_cast<std::uint16_t>(id), std::move(payload)};
}

// "-3 STATUS_WIFI_INCORRECT_CREDENTIALS": a status of the phone with its name.
std::string StatusText(int status)
{
    const std::string name(aaw::Status_Name(static_cast<aaw::Status>(status)));
    return std::to_string(status) + (name.empty() ? std::string() : " " + name);
}
}

// A conversation that hands the phone `credentials`.
WirelessHandshake::WirelessHandshake(WifiCredentials credentials) : m_credentials(std::move(credentials))
{
}

// What the head unit says first, right after the phone connected: where to connect.
WirelessHandshakeStep WirelessHandshake::Start()
{
    aaw::WifiStartRequest request;
    request.set_ip_address(m_credentials.ipAddress);
    request.set_port(m_credentials.port);
    return {{MakeMessage(WirelessMessageId::StartRequest, request.SerializeAsString())},
        "Sent the start request: the phone should connect to " + m_credentials.ipAddress + ":" + std::to_string(m_credentials.port)};
}

// Answers a message of the phone: its question for the network, its start response or its connection status; anything
// else is only logged.
WirelessHandshakeStep WirelessHandshake::OnMessage(const WirelessMessage& message)
{
    switch (static_cast<WirelessMessageId>(message.id)) {
    case WirelessMessageId::InfoRequest:
        return AnswerInfoRequest();
    case WirelessMessageId::StartResponse: {
        aaw::WifiStartResponse response;
        if (!response.ParseFromString(message.payload)) return {{}, "The phone's start response could not be read"};
        const int status = static_cast<int>(response.status());
        if (status < 0) return NoteFailure(status, "The phone could not start the wireless connection (status " + StatusText(status) + ")");
        const std::string address = response.has_ip_address() ? ", " + response.ip_address() + ":" + std::to_string(response.port()) : std::string();
        return {{}, "The phone answered the start request (status " + StatusText(status) + address + ")"};
    }
    case WirelessMessageId::ConnectionStatus: {
        aaw::WifiConnectionStatus connection;
        if (!connection.ParseFromString(message.payload)) return {{}, "The phone's connection status could not be read"};
        const int status = static_cast<int>(connection.status());
        if (status < 0) {
            const std::string details = connection.has_error_message() ? ": " + connection.error_message() : std::string();
            return NoteFailure(status, "The phone could not join the Wi-Fi (status " + StatusText(status) + details + ")");
        }
        return {{}, "The phone reports its Wi-Fi connection (status " + StatusText(status) + ")"};
    }
    default:
        return {{}, "Ignored a message from the phone: id " + std::to_string(message.id) + ", " + std::to_string(message.payload.size()) + " bytes"};
    }
}

// The phone reported that it cannot use the network (wrong password, Wi-Fi off, no suitable channel, ...).
bool WirelessHandshake::IsFailed() const
{
    return m_isFailed;
}

// What the phone reported as its failure.
const std::string& WirelessHandshake::Failure() const
{
    return m_failure;
}

// The phone's status code of that failure (aap_protobuf.aaw.Status, negative), 0 without one.
int WirelessHandshake::FailureStatus() const
{
    return m_failureStatus;
}

// Whether the phone asked for the Wi-Fi details and got them.
bool WirelessHandshake::HasSentInfo() const
{
    return m_hasSentInfo;
}

// The Wi-Fi details, in Android's numbering of the security mode (see kWifiSecurityWpa2Personal; the imported enum
// names this value WPA2_ENTERPRISE).
WirelessHandshakeStep WirelessHandshake::AnswerInfoRequest()
{
    namespace wifi = aap_protobuf::service::wifiprojection::message;
    aaw::WifiInfoResponse response;
    response.set_ssid(m_credentials.ssid);
    response.set_password(m_credentials.password);
    response.set_bssid(m_credentials.bssid);
    response.set_security_mode(static_cast<wifi::WifiSecurityMode>(kWifiSecurityWpa2Personal));
    response.set_access_point_type(wifi::DYNAMIC);
    m_hasSentInfo = true;
    return {{MakeMessage(WirelessMessageId::InfoResponse, response.SerializeAsString())},
        "The phone asked for the Wi-Fi details; sent network '" + m_credentials.ssid + "' (BSSID " + m_credentials.bssid + ")"};
}

// The phone reported a failure: remembered with its status, and logged.
WirelessHandshakeStep WirelessHandshake::NoteFailure(int status, std::string failure)
{
    m_isFailed = true;
    m_failureStatus = status;
    m_failure = std::move(failure);
    return {{}, m_failure};
}

// What a failure status of the phone means and what to do about it (German, for the window).
std::string WifiFailureAdvice(int status)
{
    switch (static_cast<aaw::Status>(status)) {
    case aaw::STATUS_WIFI_INCORRECT_CREDENTIALS:
        return "Das Handy meldet ein falsches WLAN-Passwort. Am Handy in den WLAN-Einstellungen HEATUNIT-AA 'Vergessen' "
               "und Android Auto neu verbinden (Taste Android Auto verbinden).";
    case aaw::STATUS_WIFI_DISABLED:
    case aaw::STATUS_PHONE_WIFI_DISABLED:
        return "Das WLAN am Handy ist aus: einschalten.";
    case aaw::STATUS_WIFI_INACCESSIBLE_CHANNEL:
    case aaw::STATUS_NO_SUPPORTED_WIFI_CHANNELS:
        return "Das Handy kann den WLAN-Kanal nicht nutzen: WLAN-Land pruefen, oder 2,4 GHz mit HEADUNIT_WIFI_BAND=bg.";
    case aaw::STATUS_WIFI_NETWORK_UNAVAILABLE:
        return "Das Handy findet das WLAN nicht: Ist es verborgen (HEADUNIT_WIFI_HIDDEN), oder zu weit weg?";
    case aaw::STATUS_INSTRUCT_USER_TO_CHECK_THE_PHONE:
        return "Das Handy verlangt eine Bestaetigung: aufs Handy schauen.";
    default:
        return "Ist das WLAN am Handy an?";
    }
}
}
