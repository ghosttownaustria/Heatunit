#include "androidauto/ServiceDiscovery.h"
#include "androidauto/ChannelSupport.h"
#include "androidauto/DisplayService.h"
#include "androidauto/ProjectionKeys.h"
#include <aap_protobuf/service/inputsource/message/TouchScreenType.pb.h>
#include <aap_protobuf/service/sensorsource/message/SensorType.pb.h>

namespace headunit {
namespace {
namespace control = aap_protobuf::service::control::message;
namespace media = aap_protobuf::service::media::shared::message;
using aasdk::messenger::ChannelId;

// Who the head unit says it is, and how the phone should check that the link is alive.
void DescribeHeadUnit(control::ServiceDiscoveryResponse& response)
{
    response.set_display_name("HeadUnit Windows PoC");
    response.set_driver_position(control::DRIVER_POSITION_LEFT);
    response.set_probe_for_support(false);
    auto* ping = response.mutable_connection_configuration()->mutable_ping_configuration();
    ping->set_tracked_ping_count(5);
    ping->set_timeout_ms(3000);
    ping->set_interval_ms(1000);
    ping->set_high_latency_threshold_ms(200);
    auto* info = response.mutable_headunit_info();
    info->set_make("HeadUnit");
    info->set_model("Windows PoC");
    info->set_year("2026");
    info->set_vehicle_id("HeadUnit-PoC-001");
    info->set_head_unit_make("HeadUnit");
    info->set_head_unit_model("Windows");
    info->set_head_unit_software_build("1");
    info->set_head_unit_software_version("0.2");
}

// The audio sinks, in the order a known working head unit advertises them.
void AddAudioSinks(control::ServiceDiscoveryResponse& response)
{
    for (const auto& stream : kAudioStreams) {
        auto* channel = response.add_channels();
        channel->set_id(static_cast<unsigned>(stream.channel));
        auto* service = channel->mutable_media_sink_service();
        service->set_available_type(media::MEDIA_CODEC_AUDIO_PCM);
        service->set_audio_type(stream.type);
        service->set_available_while_in_call(true);
        auto* configuration = service->add_audio_configs();
        configuration->set_sampling_rate(stream.samplingRate);
        configuration->set_number_of_bits(stream.bits);
        configuration->set_number_of_channels(stream.channels);
    }
}

// The H.264 video sink for `display`.
void AddVideoSink(control::ServiceDiscoveryResponse& response, const DisplayConfig& display)
{
    auto* channel = response.add_channels();
    channel->set_id(static_cast<unsigned>(ChannelId::MEDIA_SINK_VIDEO));
    auto* service = channel->mutable_media_sink_service();
    service->set_available_type(media::MEDIA_CODEC_VIDEO_H264_BP);
    service->set_available_while_in_call(true);
    *service->add_video_configs() = BuildVideoConfiguration(display);
}

// The microphone source.
void AddMicrophone(control::ServiceDiscoveryResponse& response)
{
    auto* channel = response.add_channels();
    channel->set_id(static_cast<unsigned>(ChannelId::MEDIA_SOURCE_MICROPHONE));
    auto* microphone = channel->mutable_media_source_service();
    microphone->set_available_type(media::MEDIA_CODEC_AUDIO_PCM);
    auto* configuration = microphone->mutable_audio_config();
    configuration->set_sampling_rate(kMicrophoneSamplingRate);
    configuration->set_number_of_bits(kMicrophoneBits);
    configuration->set_number_of_channels(kMicrophoneChannels);
}

// The sensors: driving status and night mode.
void AddSensors(control::ServiceDiscoveryResponse& response)
{
    namespace sensor = aap_protobuf::service::sensorsource::message;
    auto* channel = response.add_channels();
    channel->set_id(static_cast<unsigned>(ChannelId::SENSOR));
    channel->mutable_sensor_source_service()->add_sensors()->set_sensor_type(sensor::SENSOR_DRIVING_STATUS_DATA);
    channel->mutable_sensor_source_service()->add_sensors()->set_sensor_type(sensor::SENSOR_NIGHT_MODE);
}

// The touchscreen of `display` and the car keys. Touch positions are pixels of the shown area (the phone does not count
// the margins).
void AddInput(control::ServiceDiscoveryResponse& response, const DisplayConfig& display)
{
    const VideoLayout layout = VideoLayoutOf(display);
    auto* channel = response.add_channels();
    channel->set_id(static_cast<unsigned>(ChannelId::INPUT_SOURCE));
    auto* touch = channel->mutable_input_source_service()->add_touchscreen();
    touch->set_width(layout.width);
    touch->set_height(layout.height);
    touch->set_type(aap_protobuf::service::inputsource::message::CAPACITIVE);
    for (const auto keycode : keys::Supported) channel->mutable_input_source_service()->add_keycodes_supported(static_cast<int>(keycode));
}
}

// The head unit's answer to the phone's service discovery: its identity and every service it offers (audio, video for
// `display`, microphone, sensors, touch and keys).
control::ServiceDiscoveryResponse BuildServiceDiscoveryResponse(const DisplayConfig& display)
{
    control::ServiceDiscoveryResponse response;
    response.mutable_channels()->Reserve(8);
    DescribeHeadUnit(response);
    AddAudioSinks(response);
    AddVideoSink(response, display);
    AddMicrophone(response);
    AddSensors(response);
    AddInput(response, display);
    return response;
}
}
