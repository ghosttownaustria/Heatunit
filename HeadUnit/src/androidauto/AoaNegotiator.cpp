#include "androidauto/AoaNegotiator.h"
#include <array>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace headunit {
// Sends the AOA identity strings, then START. Throws when a step fails; the caller must then verify that the phone
// re-enumerates in accessory mode, because a phone that disconnects before answering START is not a proof of success.
void RequestAccessoryMode(IUsbControl& control, const std::function<void(const std::string&)>& reportStage)
{
    // Manufacturer, model, description, version (must not be empty), URI, serial. "Android" / "Android Auto" is what
    // identifies the projection accessory to the phone.
    constexpr std::array<std::string_view, 6> kIdentity{"Android", "Android Auto", "HeadUnit Windows PoC", "1.0", "", "HeadUnit-PoC-001"};
    for (std::uint16_t index = 0; index < kIdentity.size(); ++index) {
        reportStage("AOA SEND_STRING (52), index=" + std::to_string(index));
        std::vector<std::uint8_t> bytes(kIdentity[index].begin(), kIdentity[index].end());
        bytes.push_back(0);
        const auto result = control.Transfer(kAoaVendorOut, kAoaSendString, index, bytes);
        if (!result.error.empty()) throw std::runtime_error(result.error);
        if (result.transferred != static_cast<int>(bytes.size()))
            throw std::runtime_error("Incomplete AOA identity transfer; START was not sent");
    }
    reportStage("AOA START (53)");
    const auto result = control.Transfer(kAoaVendorOut, kAoaStart, 0, {});
    // Some phones disconnect before the completion is reported. This is NOT success: only seeing the accessory
    // product id on the bus confirms it.
    if (!result.error.empty() && !result.isDisconnected) throw std::runtime_error(result.error);
    if (result.error.empty() && result.transferred != 0) throw std::runtime_error("Invalid AOA START response length");
}
}
