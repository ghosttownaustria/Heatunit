#include "androidauto/AudioSinkChannel.h"
#include <aap_protobuf/service/media/source/message/Ack.pb.h>
#include <utility>

namespace headunit {
// A handler for `stream`; `opener` (may be empty) opens the speaker on the first samples, `report` receives the lines for
// the window.
AudioSinkChannel::AudioSinkChannel(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger, const AudioStreamSpec& stream,
    AudioOpener opener, std::function<void(const std::string&)> report)
    : m_strand(strand), m_name(AudioKindName(stream.kind)), m_kind(stream.kind), m_format{stream.samplingRate, stream.channels, stream.bits},
      m_opener(std::move(opener)), m_report(std::move(report)),
      m_channel(std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(strand, std::move(messenger), stream.channel))
{
}

// Starts listening on the channel.
void AudioSinkChannel::Start()
{
    m_channel->receive(shared_from_this());
}

// The phone opens the stream's channel: accepted.
void AudioSinkChannel::onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest&)
{
    m_report("Phone opened the " + m_name + " audio channel");
    m_channel->sendChannelOpenResponse(SuccessfulOpenResponse(), Promise());
    m_channel->receive(shared_from_this());
}

// The phone sets the stream up: ready.
void AudioSinkChannel::onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup&)
{
    m_channel->sendChannelSetupResponse(ReadyMediaConfig(), Promise());
    m_channel->receive(shared_from_this());
}

// The phone starts playing; its session id goes into the acknowledgements.
void AudioSinkChannel::onMediaChannelStartIndication(const aap_protobuf::service::media::shared::message::Start& indication)
{
    m_session = indication.session_id();
    m_channel->receive(shared_from_this());
}

// The phone stops playing: what is still queued is dropped.
void AudioSinkChannel::onMediaChannelStopIndication(const aap_protobuf::service::media::shared::message::Stop&)
{
    m_session = -1;
    if (m_output) m_output->Flush();
    m_channel->receive(shared_from_this());
}

// A block of samples: played and acknowledged.
void AudioSinkChannel::onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer)
{
    Play(buffer);
    aap_protobuf::service::media::source::message::Ack ack;
    ack.set_session_id(m_session);
    ack.set_ack(1);
    m_channel->sendMediaAckIndication(ack, Promise());
    m_channel->receive(shared_from_this());
}

// A block of samples without a timestamp: handled like one with.
void AudioSinkChannel::onMediaIndication(const aasdk::common::DataConstBuffer& buffer)
{
    onMediaWithTimestampIndication(0, buffer);
}

// Audio is not required for projection: an error is reported, but the video session keeps running. An abort is just the
// session shutting down, not a failure worth reporting.
void AudioSinkChannel::onChannelError(const aasdk::error::Error& error)
{
    if (IsAbortError(error)) return;
    m_report(m_name + " audio channel stopped: " + error.what());
}

// Hands the samples to the speaker, which is opened on the first samples, so that a stream the phone never uses never
// touches it. Without an output device the samples are dropped from then on.
void AudioSinkChannel::Play(const aasdk::common::DataConstBuffer& buffer)
{
    if (!m_opener) return;
    if (!m_output) {
        m_output = m_opener(m_kind, m_format);
        m_report(m_name + " audio started: " + std::to_string(m_format.sampleRate) + " Hz, " + std::to_string(m_format.channels) + " channel(s)" +
            (m_output ? "" : " (no output device)"));
        if (!m_output) m_opener = nullptr;
    }
    if (m_output) m_output->Write({buffer.cdata, buffer.size});
}

// A send promise that reports a failure as a channel error.
aasdk::channel::SendPromise::Pointer AudioSinkChannel::Promise()
{
    return MakeSendPromise(m_strand, [] {}, [self = shared_from_this()](const aasdk::error::Error& error) { self->onChannelError(error); });
}
}
