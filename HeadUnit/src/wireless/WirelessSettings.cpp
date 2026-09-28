#include "wireless/WirelessSettings.h"
#include "platform/Environment.h"
#include <QSettings>
#include <QString>
#include <charconv>
#include <optional>
#include <random>

namespace headunit {
namespace {
constexpr const char* kPasswordSetting = "wireless/wifiPassword";
// Set once a phone did not join the hidden network (see WirelessStation::ShowWifi).
constexpr const char* kVisibleSetting = "wireless/wifiVisible";
constexpr std::size_t kMinimumPasswordLength = 8;
constexpr int kRandomPasswordLength = 16;

// A new Wi-Fi password without look-alike characters.
std::string RandomPassword()
{
    static constexpr char kAlphabet[] = "abcdefghjkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::random_device device;
    std::uniform_int_distribution<std::size_t> pick(0, sizeof(kAlphabet) - 2);
    std::string password;
    for (int index = 0; index < kRandomPasswordLength; ++index) password += kAlphabet[pick(device)];
    return password;
}

// The phone joins this network by itself, told over Bluetooth, so the password never has to be typed: it only has to
// stay the same between runs, so that the phone's remembered copy of the network keeps working.
std::string RememberedPassword()
{
    QSettings settings;
    auto password = settings.value(kPasswordSetting).toString().toStdString();
    if (password.size() < kMinimumPasswordLength) {
        password = RandomPassword();
        settings.setValue(kPasswordSetting, QString::fromStdString(password));
        settings.sync();
    }
    return password;
}

// The value of a variable that is set and not empty.
std::optional<std::string> NonEmptyEnv(const char* name)
{
    auto value = GetEnv(name);
    if (!value || value->empty()) return std::nullopt;
    return value;
}
}

// The wireless settings from the environment, with the defaults for what is not set.
WirelessSettings LoadWirelessSettings()
{
    WirelessSettings settings;
    if (const auto name = NonEmptyEnv("HEADUNIT_BT_NAME")) settings.bluetoothName = *name;
    auto& hotspot = settings.hotspot;
    if (const auto ssid = NonEmptyEnv("HEADUNIT_WIFI_SSID")) hotspot.ssid = *ssid;
    if (const auto interfaceName = GetEnv("HEADUNIT_WIFI_INTERFACE")) hotspot.interfaceName = *interfaceName;
    const auto password = NonEmptyEnv("HEADUNIT_WIFI_PASSWORD");
    hotspot.password = password ? *password : RememberedPassword();
    if (const auto band = GetEnv("HEADUNIT_WIFI_BAND"); band && (*band == "a" || *band == "bg")) {
        hotspot.band = *band;
        hotspot.channel = *band == "a" ? 36 : 6;
    }
    if (const auto channel = GetEnv("HEADUNIT_WIFI_CHANNEL")) {
        int value = 0;
        if (std::from_chars(channel->data(), channel->data() + channel->size(), value).ec == std::errc{} && value > 0) hotspot.channel = value;
    }
    if (const auto hidden = NonEmptyEnv("HEADUNIT_WIFI_HIDDEN")) {
        hotspot.isHidden = !(*hidden == "0" || *hidden == "no" || *hidden == "false");
        settings.isVisibilityFixed = true;
    } else if (QSettings().value(kVisibleSetting).toBool()) {
        hotspot.isHidden = false;
    }
    return settings;
}

// A phone did not join the hidden network: it is broadcast from now on, also in later runs.
void RememberVisibleWifi()
{
    QSettings settings;
    settings.setValue(kVisibleSetting, true);
    settings.sync();
}
}
