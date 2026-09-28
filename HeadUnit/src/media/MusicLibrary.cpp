#include "media/MusicLibrary.h"
#include <algorithm>
#include <cctype>
#include <iterator>
#include <system_error>

namespace headunit {
namespace {
// The end of the run of digits that starts at `start`.
std::size_t DigitsEnd(std::string_view text, std::size_t start)
{
    while (start < text.size() && std::isdigit(static_cast<unsigned char>(text[start]))) ++start;
    return start;
}

// The number `text[start, end)` without its leading zeros (one digit stays).
std::string_view WithoutLeadingZeros(std::string_view text, std::size_t start, std::size_t end)
{
    while (start + 1 < end && text[start] == '0') ++start;
    return text.substr(start, end - start);
}

// The order of the list: the files at the top of the folder first, then folder by folder, each in natural order.
bool TrackLess(const MusicTrack& first, const MusicTrack& second)
{
    if (first.folder.empty() != second.folder.empty()) return first.folder.empty();
    if (NaturalLess(first.folder, second.folder)) return true;
    if (NaturalLess(second.folder, first.folder)) return false;
    if (NaturalLess(first.name, second.name)) return true;
    if (NaturalLess(second.name, first.name)) return false;
    return first.path < second.path;   // names that differ only in case or leading zeros: any fixed order
}
}

// UTF-8 text of a path (std::filesystem uses UTF-16 on Windows).
std::string PathUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The file types the player decodes; everything else in the folder (pictures, playlists) is left alone.
bool IsMusicFile(const std::filesystem::path& path)
{
    std::string extension = PathUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    static constexpr std::string_view kExtensions[] = {".mp3", ".flac", ".m4a", ".aac", ".ogg", ".oga", ".opus", ".wav", ".wma", ".aif", ".aiff", ".alac"};
    return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions);
}

// Orders names the way people number their files: "2 Song" before "10 Song", letters without regard to case.
bool NaturalLess(std::string_view first, std::string_view second)
{
    std::size_t firstIndex = 0;
    std::size_t secondIndex = 0;
    while (firstIndex < first.size() && secondIndex < second.size()) {
        const auto firstCharacter = static_cast<unsigned char>(first[firstIndex]);
        const auto secondCharacter = static_cast<unsigned char>(second[secondIndex]);
        if (std::isdigit(firstCharacter) && std::isdigit(secondCharacter)) {
            // Numbers are compared without their leading zeros: first by length, then digit by digit.
            const std::size_t firstEnd = DigitsEnd(first, firstIndex);
            const std::size_t secondEnd = DigitsEnd(second, secondIndex);
            const std::string_view firstNumber = WithoutLeadingZeros(first, firstIndex, firstEnd);
            const std::string_view secondNumber = WithoutLeadingZeros(second, secondIndex, secondEnd);
            if (firstNumber.size() != secondNumber.size()) return firstNumber.size() < secondNumber.size();
            if (firstNumber != secondNumber) return firstNumber < secondNumber;
            firstIndex = firstEnd;
            secondIndex = secondEnd;
            continue;
        }
        const int firstLower = std::tolower(firstCharacter);
        const int secondLower = std::tolower(secondCharacter);
        if (firstLower != secondLower) return firstLower < secondLower;
        ++firstIndex;
        ++secondIndex;
    }
    return first.size() - firstIndex < second.size() - secondIndex;
}

// Every music file below `folder`, sub folders included (one per album, say), in the order of the list (TrackLess), at
// most `limit`. A missing or unreadable folder gives an empty list; unreadable sub folders are skipped.
std::vector<MusicTrack> ScanMusicFolder(const std::filesystem::path& folder, std::size_t limit)
{
    namespace fs = std::filesystem;
    std::vector<MusicTrack> tracks;
    std::error_code error;
    fs::recursive_directory_iterator entry(folder, fs::directory_options::skip_permission_denied, error);
    for (const fs::recursive_directory_iterator end; !error && entry != end && tracks.size() < limit; entry.increment(error)) {
        std::error_code typeError;
        if (!entry->is_regular_file(typeError) || !IsMusicFile(entry->path())) continue;
        const fs::path relative = entry->path().lexically_relative(folder);
        tracks.push_back({PathUtf8(entry->path()), PathUtf8(entry->path().stem()), PathUtf8(relative.parent_path())});
    }
    std::sort(tracks.begin(), tracks.end(), TrackLess);
    return tracks;
}
}
