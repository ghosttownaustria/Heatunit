#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace headunit {
// One file of the music folder.
struct MusicTrack {
    std::string path;     // UTF-8, what the player opens
    std::string name;     // the file name without its extension, as the list shows it
    std::string folder;   // sub folder relative to the music folder, "" at the top ("Album" or "Artist/Album")
};

std::string PathUtf8(const std::filesystem::path& path);
bool IsMusicFile(const std::filesystem::path& path);
bool NaturalLess(std::string_view first, std::string_view second);
std::vector<MusicTrack> ScanMusicFolder(const std::filesystem::path& folder, std::size_t limit = 10000);
}
