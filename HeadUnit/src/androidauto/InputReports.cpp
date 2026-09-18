#include "androidauto/InputReports.h"
#include <aap_protobuf/service/inputsource/message/KeyEvent.pb.h>
#include <aap_protobuf/service/inputsource/message/PointerAction.pb.h>
#include <aap_protobuf/service/inputsource/message/RelativeEvent.pb.h>
#include <aap_protobuf/service/inputsource/message/TouchEvent.pb.h>

namespace headunit {
namespace input = aap_protobuf::service::inputsource::message;

input::InputReport BuildInputReport(const InputEvent& event, std::uint64_t timestampNs)
{
    input::InputReport report;
    report.set_timestamp(timestampNs);
    if (const auto* touch = std::get_if<TouchInput>(&event)) {
        auto* touchEvent = report.mutable_touch_event();
        auto* pointer = touchEvent->add_pointer_data();
        pointer->set_x(static_cast<std::uint32_t>(touch->x));
        pointer->set_y(static_cast<std::uint32_t>(touch->y));
        pointer->set_pointer_id(touch->pointerId);
        touchEvent->set_action_index(0);
        touchEvent->set_action(touch->action == TouchAction::Down ? input::ACTION_DOWN
            : touch->action == TouchAction::Up ? input::ACTION_UP : input::ACTION_MOVED);
    } else if (const auto* key = std::get_if<KeyInput>(&event)) {
        auto* pressed = report.mutable_key_event()->add_keys();
        pressed->set_keycode(key->keycode);
        pressed->set_down(key->isDown);
        pressed->set_metastate(0);
    } else if (const auto* rotary = std::get_if<RotaryInput>(&event)) {
        auto* turn = report.mutable_relative_event()->add_data();
        turn->set_keycode(keys::RotaryController);
        turn->set_delta(rotary->delta);
    }
    return report;
}
}
