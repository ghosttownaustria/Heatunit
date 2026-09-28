#include "media/StreamText.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace headunit {
namespace {
// The length of the UTF-8 sequence that starts with `lead`, 0 for a byte that cannot start one.
std::size_t Utf8SequenceLength(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead >> 5) == 0x6) return 2;
    if ((lead >> 4) == 0xE) return 3;
    if ((lead >> 3) == 0x1E) return 4;
    return 0;
}

// `text` without spaces at either end.
std::string_view Trimmed(std::string_view text)
{
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

// A number of at least two digits ("07").
std::string TwoDigits(std::int64_t value)
{
    return (value < 10 ? "0" : "") + std::to_string(value);
}
}

// Whether `text` is well-formed UTF-8 (lead and continuation bytes; overlong forms are not checked).
bool IsValidUtf8(std::string_view text)
{
    for (std::size_t index = 0; index < text.size();) {
        const std::size_t length = Utf8SequenceLength(static_cast<unsigned char>(text[index]));
        if (length == 0 || index + length > text.size()) return false;
        for (std::size_t offset = 1; offset < length; ++offset) {
            if ((static_cast<unsigned char>(text[index + offset]) & 0xC0) != 0x80) return false;
        }
        index += length;
    }
    return true;
}

// Latin-1 text as UTF-8.
std::string Latin1ToUtf8(std::string_view text)
{
    std::string result;
    for (const char character : text) {
        const auto value = static_cast<unsigned char>(character);
        if (value < 0x80) {
            result += character;
            continue;
        }
        result += static_cast<char>(0xC0 | (value >> 6));
        result += static_cast<char>(0x80 | (value & 0x3F));
    }
    return result;
}

// What an internet radio station says is playing: the StreamTitle of its ICY metadata packet
// ("StreamTitle='Artist - Title';StreamUrl='...';"). Titles that are not UTF-8 are read as Latin-1, as older stations
// send them. Empty when the packet has no title.
std::string IcyStreamTitle(std::string_view packet)
{
    constexpr std::string_view kKey = "StreamTitle='";
    const std::size_t start = packet.find(kKey);
    if (start == std::string_view::npos) return {};
    const std::size_t from = start + kKey.size();
    // A title may contain an apostrophe ("Guns N' Roses"); it ends at "';" or, failing that, at the last one.
    std::size_t end = packet.find("';", from);
    if (end == std::string_view::npos) end = packet.rfind('\'');
    if (end == std::string_view::npos || end < from) return {};
    const std::string_view title = Trimmed(packet.substr(from, end - from));
    if (title == "-") return {};
    return IsValidUtf8(title) ? std::string(title) : Latin1ToUtf8(title);
}

// A play time as a car radio shows it: "3:07", "1:02:05"; negative or unknown times as "0:00".
std::string FormatPlayTime(double seconds)
{
    const auto total = static_cast<std::int64_t>(std::isfinite(seconds) && seconds > 0 ? seconds : 0);
    const std::int64_t hours = total / 3600;
    const std::int64_t minutes = total / 60 % 60;
    const std::int64_t rest = total % 60;
    if (hours == 0) return std::to_string(minutes) + ":" + TwoDigits(rest);
    return std::to_string(hours) + ":" + TwoDigits(minutes) + ":" + TwoDigits(rest);
}
}
