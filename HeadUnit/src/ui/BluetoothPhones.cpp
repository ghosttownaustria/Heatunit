#include "ui/BluetoothPhones.h"
#include <algorithm>
#include <cctype>
#include <tuple>

namespace headunit {
namespace {
// `text` in lower case, so that the list does not sort capitals first.
std::string Lowered(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return text;
}
}

// The order of the Bluetooth page: the Android Auto phone first, then the connected ones, then the others, each group by
// name.
std::vector<BluetoothPhone> SortedPhones(std::vector<BluetoothPhone> phones)
{
    std::stable_sort(phones.begin(), phones.end(), [](const BluetoothPhone& first, const BluetoothPhone& second) {
        return std::make_tuple(!first.isAndroidAuto, !first.isConnected, Lowered(first.name)) <
            std::make_tuple(!second.isAndroidAuto, !second.isConnected, Lowered(second.name));
    });
    return phones;
}

// The note at the right of a phone's row.
std::string PhoneStateText(const BluetoothPhone& phone)
{
    if (phone.isAndroidAuto) return "Android Auto";
    return phone.isConnected ? "Connected" : "Not connected";
}

// Whether choosing the phone does something: every phone but the one Android Auto runs on already.
bool CanSwitchTo(const BluetoothPhone& phone)
{
    return !phone.isAndroidAuto && !phone.id.empty();
}
}
