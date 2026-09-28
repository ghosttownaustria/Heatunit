#pragma once
#include "androidauto/ChannelSupport.h"
#include <aasdk/Channel/MediaSource/IMediaSourceServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSource/MediaSourceService.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <functional>
#include <memory>
#include <string>

namespace headunit {
// Advertises a microphone so that voice input is negotiable, and answers its requests. No samples are produced. Runs on
// the session's strand.
class MicrophoneChannel final : public aasdk::channel::mediasource::IMediaSourceServiceEventHandler,
                                public std::enable_shared_from_this<MicrophoneChannel> {
public:
    MicrophoneChannel(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger, std::function<void(const std::string&)> report);

    void Start();
    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override;
    void onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup& request) override;
    void onMediaSourceOpenRequest(const aap_protobuf::service::media::source::message::MicrophoneRequest& request) override;
    void onMediaChannelAckIndication(const aap_protobuf::service::media::source::message::Ack& indication) override;
    void onChannelError(const aasdk::error::Error& error) override;

private:
    boost::asio::io_context::strand& m_strand;
    std::function<void(const std::string&)> m_report;
    std::shared_ptr<aasdk::channel::mediasource::MediaSourceService> m_channel;

    aasdk::channel::SendPromise::Pointer Promise();
};
}
