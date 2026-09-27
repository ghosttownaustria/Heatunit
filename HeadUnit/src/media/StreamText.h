#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace headunit {
inline bool IsValidUtf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size()) return false;
        for (std::size_t k = 1; k < length; ++k)
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        i += length;
    }
    return true;
}
inline std::string Latin1ToUtf8(std::string_view text)
{
    std::string result;
    for (const char c : text) {
        const auto value = static_cast<unsigned char>(c);
        if (value < 0x80) { result += c; continue; }
        result += static_cast<char>(0xC0 | (value >> 6));
        result += static_cast<char>(0x80 | (value & 0x3F));
    }
    return result;
}

// What an internet radio station says is playing: the StreamTitle of its ICY metadata packet
// ("StreamTitle='Artist - Title';StreamUrl='...';"). Titles that are not UTF-8 are read as Latin-1, as older
// stations send them. Empty when the packet has no title.
inline std::string IcyStreamTitle(std::string_view packet)
{
    constexpr std::string_view kKey = "StreamTitle='";
    const std::size_t start = packet.find(kKey);
    if (start == std::string_view::npos) return {};
    const std::size_t from = start + kKey.size();
    // A title may contain an apostrophe ("Guns N' Roses"); it ends at "';" or, failing that, at the last one.
    std::size_t end = packet.find("';", from);
    if (end == std::string_view::npos) end = packet.rfind('\'');
    if (end == std::string_view::npos || end < from) return {};
    std::string_view title = packet.substr(from, end - from);
    while (!title.empty() && (title.back() == ' ' || title.front() == ' ')) {
        if (title.back() == ' ') title.remove_suffix(1);
        else title.remove_prefix(1);
    }
    if (title == "-") return {};
    return IsValidUtf8(title) ? std::string(title) : Latin1ToUtf8(title);
}

// A play time as a car radio shows it: "3:07", "1:02:05"; negative or unknown times as "0:00".
inline std::string FormatPlayTime(double seconds)
{
    const auto total = static_cast<std::int64_t>(std::isfinite(seconds) && seconds > 0 ? seconds : 0);
    const std::int64_t hours = total / 3600, minutes = total / 60 % 60, rest = total % 60;
    const std::string twoDigits = (rest < 10 ? "0" : "") + std::to_string(rest);
    if (hours == 0) return std::to_string(minutes) + ":" + twoDigits;
    return std::to_string(hours) + ":" + (minutes < 10 ? "0" : "") + std::to_string(minutes) + ":" + twoDigits;
}
}
