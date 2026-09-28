#pragma once
#include "androidauto/ChannelSupport.h"
#include "audio/IPcmOutput.h"
#include <aasdk/Channel/MediaSink/Audio/AudioMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Audio/IAudioMediaSinkServiceEventHandler.hpp>
#include <aasdk/Messenger/IMessenger.hpp>
#include <functional>
#include <memory>
#include <string>

namespace headunit {
// One of the phone's audio streams: accepts the advertised sink, plays what the phone sends and acknowledges every
// payload. Without an output the samples are dropped, which keeps the channel alive; refusing the channel would end the
// phone's session. Runs on the session's strand.
class AudioSinkChannel final : public aasdk::channel::mediasink::audio::IAudioMediaSinkServiceEventHandler,
                               public std::enable_shared_from_this<AudioSinkChannel> {
public:
    AudioSinkChannel(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger, const AudioStreamSpec& stream,
        AudioOpener opener, std::function<void(const std::string&)> report);

    void Start();
    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override;
    void onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup& request) override;
    void onMediaChannelStartIndication(const aap_protobuf::service::media::shared::message::Start& indication) override;
    void onMediaChannelStopIndication(const aap_protobuf::service::media::shared::message::Stop& indication) override;
    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override;
    void onChannelError(const aasdk::error::Error& error) override;

private:
    boost::asio::io_context::strand& m_strand;
    std::string m_name;
    AudioKind m_kind;
    PcmFormat m_format;
    AudioOpener m_opener;
    std::shared_ptr<IPcmOutput> m_output;
    std::function<void(const std::string&)> m_report;
    std::shared_ptr<aasdk::channel::mediasink::audio::AudioMediaSinkService> m_channel;
    int m_session{-1};

    void Play(const aasdk::common::DataConstBuffer& buffer);
    aasdk::channel::SendPromise::Pointer Promise();
};
}
