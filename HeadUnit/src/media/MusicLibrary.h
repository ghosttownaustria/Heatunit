#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace headunit {
// One file of the music folder.
struct MusicTrack {
    std::string path;     // UTF-8, what the player opens
    std::string name;     // the file name without its extension, as the list shows it
    std::string folder;   // sub folder relative to the music folder, "" at the top ("Album" or "Artist/Album")
};

// UTF-8 text of a path (std::filesystem uses UTF-16 on Windows).
inline std::string PathUtf8(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

// The file types the player decodes; everything else in the folder (pictures, playlists) is left alone.
inline bool IsMusicFile(const std::filesystem::path& path)
{
    std::string extension = PathUtf8(path.extension());
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    static constexpr std::string_view kExtensions[] = {".mp3", ".flac", ".m4a", ".aac", ".ogg", ".oga", ".opus", ".wav", ".wma", ".aif", ".aiff", ".alac"};
    return std::find(std::begin(kExtensions), std::end(kExtensions), extension) != std::end(kExtensions);
}

// Orders names the way people number their files: "2 Song" before "10 Song", letters without regard to case.
inline bool NaturalLess(std::string_view a, std::string_view b)
{
    std::size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        const auto ca = static_cast<unsigned char>(a[i]), cb = static_cast<unsigned char>(b[j]);
        if (std::isdigit(ca) && std::isdigit(cb)) {
            std::size_t endA = i, endB = j;
            while (endA < a.size() && std::isdigit(static_cast<unsigned char>(a[endA]))) ++endA;
            while (endB < b.size() && std::isdigit(static_cast<unsigned char>(b[endB]))) ++endB;
            // Compare the numbers without their leading zeros: first by length, then digit by digit.
            std::size_t startA = i, startB = j;
            while (startA + 1 < endA && a[startA] == '0') ++startA;
            while (startB + 1 < endB && b[startB] == '0') ++startB;
            const std::string_view numberA = a.substr(startA, endA - startA), numberB = b.substr(startB, endB - startB);
            if (numberA.size() != numberB.size()) return numberA.size() < numberB.size();
            if (numberA != numberB) return numberA < numberB;
            i = endA;
            j = endB;
            continue;
        }
        const int la = std::tolower(ca), lb = std::tolower(cb);
        if (la != lb) return la < lb;
        ++i;
        ++j;
    }
    return a.size() - i < b.size() - j;
}

// Every music file below `folder`, sub folders included (one per album, say): the files at the top first, then
// folder by folder, each in natural order. A missing or unreadable folder gives an empty list; unreadable sub
// folders are skipped.
inline std::vector<MusicTrack> ScanMusicFolder(const std::filesystem::path& folder, std::size_t limit = 10000)
{
    namespace fs = std::filesystem;
    std::vector<MusicTrack> tracks;
    std::error_code error;
    fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, error);
    for (const fs::recursive_directory_iterator end; !error && it != end && tracks.size() < limit; it.increment(error)) {
        std::error_code typeError;
        if (!it->is_regular_file(typeError) || !IsMusicFile(it->path())) continue;
        const fs::path relative = it->path().lexically_relative(folder);
        tracks.push_back({PathUtf8(it->path()), PathUtf8(it->path().stem()), PathUtf8(relative.parent_path())});
    }
    std::sort(tracks.begin(), tracks.end(), [](const MusicTrack& a, const MusicTrack& b) {
        if (a.folder.empty() != b.folder.empty()) return a.folder.empty();
        if (NaturalLess(a.folder, b.folder)) return true;
        if (NaturalLess(b.folder, a.folder)) return false;
        if (NaturalLess(a.name, b.name)) return true;
        if (NaturalLess(b.name, a.name)) return false;
        return a.path < b.path;   // names that differ only in case or leading zeros: any fixed order
    });
    return tracks;
}
}
