#pragma once
#include "androidauto/InputEvents.h"
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>
#include <cstdint>

namespace headunit {
aap_protobuf::service::inputsource::message::InputReport BuildInputReport(const InputEvent& event, std::uint64_t timestampNs);
}
