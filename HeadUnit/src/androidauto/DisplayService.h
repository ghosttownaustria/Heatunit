#pragma once
#include "androidauto/DisplayConfig.h"
#include <aap_protobuf/service/media/sink/message/VideoCodecResolutionType.pb.h>
#include <aap_protobuf/service/media/sink/message/VideoConfiguration.pb.h>
#include <optional>

namespace headunit {
std::optional<aap_protobuf::service::media::sink::message::VideoCodecResolutionType> VideoResolutionOf(const DisplayConfig& display);
aap_protobuf::service::media::sink::message::VideoConfiguration BuildVideoConfiguration(const DisplayConfig& display);
}
