#pragma once
#include "androidauto/DisplayConfig.h"
#include <aap_protobuf/service/media/sink/message/VideoConfiguration.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoFrameRateType.pb.h>
#include <optional>

namespace headunit {
// The resolution constant Android Auto knows for the frame that carries a display; none for a display that
// no fixed resolution holds.
inline std::optional<aap_protobuf::service::media::sink::message::VideoCodecResolutionType> VideoResolutionOf(const DisplayConfig& display)
{
    namespace sink = aap_protobuf::service::media::sink::message;
    const auto layout = VideoLayoutOf(display);
    if (layout.codecWidth == 800 && layout.codecHeight == 480) return sink::VIDEO_800x480;
    if (layout.codecWidth == 1280 && layout.codecHeight == 720) return sink::VIDEO_1280x720;
    if (layout.codecWidth == 1920 && layout.codecHeight == 1080) return sink::VIDEO_1920x1080;
    return std::nullopt;
}

// The video service's description of `display`: the frame's resolution, frame rate, the margins that fit the
// display into the frame (see VideoLayout) and the screen density.
inline aap_protobuf::service::media::sink::message::VideoConfiguration BuildVideoConfiguration(const DisplayConfig& display)
{
    namespace sink = aap_protobuf::service::media::sink::message;
    const auto layout = VideoLayoutOf(display);
    sink::VideoConfiguration configuration;
    configuration.set_codec_resolution(VideoResolutionOf(display).value_or(sink::VIDEO_800x480));
    configuration.set_frame_rate(sink::VIDEO_FPS_30);
    configuration.set_width_margin(static_cast<std::uint32_t>(layout.marginWidth));
    configuration.set_height_margin(static_cast<std::uint32_t>(layout.marginHeight));
    configuration.set_density(static_cast<std::uint32_t>(DisplayDensity(display)));
    return configuration;
}
}
