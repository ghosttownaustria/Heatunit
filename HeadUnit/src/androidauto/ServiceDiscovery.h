#pragma once
#include "androidauto/DisplayConfig.h"
#include <aap_protobuf/service/control/message/ServiceDiscoveryResponse.pb.h>

namespace headunit {
aap_protobuf::service::control::message::ServiceDiscoveryResponse BuildServiceDiscoveryResponse(const DisplayConfig& display);
}
