#include "androidauto/MicrophoneChannel.h"
#include <aap_protobuf/service/media/source/message/MicrophoneResponse.pb.h>
#include <aap_protobuf/shared/MessageStatus.pb.h>
#include <utility>

namespace headunit {
// A handler for the microphone channel; `report` receives the lines for the window.
MicrophoneChannel::MicrophoneChannel(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger,
    std::function<void(const std::string&)> report)
    : m_strand(strand), m_report(std::move(report)),
      m_channel(std::make_shared<aasdk::channel::mediasource::MediaSourceService>(strand, std::move(messenger),
          aasdk::messenger::ChannelId::MEDIA_SOURCE_MICROPHONE))
{
}

// Starts listening on the channel.
void MicrophoneChannel::Start()
{
    m_channel->receive(shared_from_this());
}

// The phone opens the microphone channel: accepted.
void MicrophoneChannel::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest&)
{
    m_report("Phone opened the microphone channel");
    m_channel->sendChannelOpenResponse(SuccessfulOpenResponse(), Promise());
    m_channel->receive(shared_from_this());
}

// The phone sets the microphone up: ready.
void MicrophoneChannel::onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup&)
{
    m_channel->sendChannelSetupResponse(ReadyMediaConfig(), Promise());
    m_channel->receive(shared_from_this());
}

// The phone opens or closes the microphone: acknowledged, although nothing is captured.
void MicrophoneChannel::onMediaSourceOpenRequest(const aap_protobuf::service::media::source::message::MicrophoneRequest& request)
{
    aap_protobuf::service::media::source::message::MicrophoneResponse response;
    response.set_session_id(0);
    response.set_status(aap_protobuf::shared::STATUS_SUCCESS);
    m_report(request.open() ? "Microphone open acknowledged; this build captures no audio" : "Microphone close acknowledged");
    m_channel->sendMicrophoneOpenResponse(response, Promise());
    m_channel->receive(shared_from_this());
}

// The phone acknowledges microphone data (never sent): keep listening.
void MicrophoneChannel::onMediaChannelAckIndication(const aap_protobuf::service::media::source::message::Ack&)
{
    m_channel->receive(shared_from_this());
}

// Reports a failure of the channel; an abort is just the session shutting down.
void MicrophoneChannel::onChannelError(const aasdk::error::Error& error)
{
    if (IsAbortError(error)) return;
    m_report(std::string("Microphone channel stopped: ") + error.what());
}

// A send promise that reports a failure as a channel error.
aasdk::channel::SendPromise::Pointer MicrophoneChannel::Promise()
{
    return MakeSendPromise(m_strand, [] {}, [self = shared_from_this()](const aasdk::error::Error& error) { self->onChannelError(error); });
}
}
