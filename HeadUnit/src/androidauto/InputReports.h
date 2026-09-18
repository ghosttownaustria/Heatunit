#pragma once
#include "androidauto/ProjectionInput.h"
#include <cstdint>
#include <aap_protobuf/service/inputsource/message/InputReport.pb.h>

namespace headunit {
// The protocol message for one simulated input: a touch point, a key press/release or a turn of
// the rotary controller. `timestampNs` is a monotonic clock in nanoseconds.
aap_protobuf::service::inputsource::message::InputReport BuildInputReport(const InputEvent& event, std::uint64_t timestampNs);
}
