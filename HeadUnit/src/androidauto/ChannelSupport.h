#pragma once
#include "audio/PcmFormat.h"
#include <aasdk/Channel/Promise.hpp>
#include <aasdk/Error/Error.hpp>
#include <aasdk/Messenger/ChannelId.hpp>
#include <aap_protobuf/service/control/message/ChannelOpenResponse.pb.h>
#include <aap_protobuf/service/media/shared/message/Config.pb.h>
#include <aap_protobuf/service/media/sink/message/AudioStreamType.pb.h>
#include <boost/asio.hpp>
#include <cstdint>
#include <functional>

// What the channels of an Android Auto session share: the audio streams the head unit advertises, and the answers every
// channel sends the same way.
namespace headunit {
// One of the phone's audio streams as the head unit advertises it. The phone only starts projection when the head unit
// advertises a complete set of services; a video-only description is accepted by the discovery step and then silently
// abandoned, so the audio sinks and the microphone source are always advertised and answered.
struct AudioStreamSpec {
    aasdk::messenger::ChannelId channel;
    aap_protobuf::service::media::sink::message::AudioStreamType type;
    std::uint32_t samplingRate;
    std::uint32_t bits;
    std::uint32_t channels;
    AudioKind kind;
};

// In the order a known working head unit advertises them.
inline const AudioStreamSpec kAudioStreams[] = {
    {aasdk::messenger::ChannelId::MEDIA_SINK_MEDIA_AUDIO, aap_protobuf::service::media::sink::message::AUDIO_STREAM_MEDIA, 48000, 16, 2, AudioKind::Media},
    {aasdk::messenger::ChannelId::MEDIA_SINK_GUIDANCE_AUDIO, aap_protobuf::service::media::sink::message::AUDIO_STREAM_GUIDANCE, 16000, 16, 1, AudioKind::Guidance},
    {aasdk::messenger::ChannelId::MEDIA_SINK_SYSTEM_AUDIO, aap_protobuf::service::media::sink::message::AUDIO_STREAM_SYSTEM_AUDIO, 16000, 16, 1, AudioKind::System},
};

inline constexpr std::uint32_t kMicrophoneSamplingRate = 16000;
inline constexpr std::uint32_t kMicrophoneBits = 16;
inline constexpr std::uint32_t kMicrophoneChannels = 1;

aasdk::channel::SendPromise::Pointer MakeSendPromise(boost::asio::io_context::strand& strand, std::function<void()> onSent,
    std::function<void(const aasdk::error::Error&)> onError);
aap_protobuf::service::control::message::ChannelOpenResponse SuccessfulOpenResponse();
aap_protobuf::service::media::shared::message::Config ReadyMediaConfig();
bool IsAbortError(const aasdk::error::Error& error);
}
