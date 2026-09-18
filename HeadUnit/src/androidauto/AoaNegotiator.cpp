#include "androidauto/AoaNegotiator.h"
#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace headunit {
void RequestAccessoryMode(IUsbControl& control, const std::function<void(const std::string&)>& reportStage)
{
    // AOA identity fields, including the required nonempty version.
    // Android/Android Auto identify the projection accessory to the phone.
    constexpr std::array<std::string_view, 6> identity{
        "Android", "Android Auto", "HeadUnit Windows PoC", "1.0", "", "HeadUnit-PoC-001"};
    for (std::uint16_t index = 0; index < identity.size(); ++index) {
        reportStage("AOA SEND_STRING (52), index=" + std::to_string(index));
        std::vector<std::uint8_t> bytes(identity[index].begin(), identity[index].end());
        bytes.push_back(0);
        const auto result = control.Transfer(0x40, 52, index, bytes);
        if (!result.error.empty()) throw std::runtime_error(result.error);
        if (result.transferred != static_cast<int>(bytes.size()))
            throw std::runtime_error("Incomplete AOA identity transfer; START was not sent");
    }
    reportStage("AOA START (53)");
    const auto result = control.Transfer(0x40, 53, 0, {});
    // Some phones disconnect before the completion is reported. This is NOT success:
    // only seeing the accessory PID on the original physical USB port confirms it.
    if (!result.error.empty() && !result.isDisconnected) throw std::runtime_error(result.error);
    if (result.error.empty() && result.transferred != 0) throw std::runtime_error("Invalid AOA START response length");
}
}
