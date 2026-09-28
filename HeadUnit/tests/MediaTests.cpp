#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "media/MusicLibrary.h"
#include "media/StreamText.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using namespace headunit;

namespace {
// The music folder: every playable file below it, in the order people number them, top folder first.
void TestMusicLibrary() {
    Check(NaturalLess("2 Song", "10 Song") && !NaturalLess("10 Song", "2 Song") && NaturalLess("track02", "Track10") &&
        NaturalLess("abc", "ABD") && NaturalLess("Song", "Song 2") && !NaturalLess("same", "same") && !NaturalLess("01 a", "1 a") && !NaturalLess("1 a", "01 a"),
        "Natural order is wrong");
    Check(IsMusicFile("a/b/Song.MP3") && IsMusicFile("x.flac") && IsMusicFile("x.m4a") && IsMusicFile("x.opus") && !IsMusicFile("cover.jpg") &&
        !IsMusicFile("list.m3u") && !IsMusicFile("mp3") && !IsMusicFile("folder.mp3/"), "Music files are not recognised");
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "headunit-music-test";
    std::error_code ignored;
    fs::remove_all(root, ignored);
    fs::create_directories(root / "Album B");
    fs::create_directories(root / "Album A" / "CD 2");
    for (const char* name : {"10 Ten.mp3", "2 Two.flac", "cover.jpg", "Album B/01 First.ogg", "Album A/3 Third.m4a", "Album A/CD 2/1 Disc two.mp3"})
        std::ofstream(root / fs::path(reinterpret_cast<const char8_t*>(name))) << "x";
    fs::create_directories(root / u8"Mötley Crüe");
    std::ofstream(root / u8"Mötley Crüe" / u8"Kickstart ü.mp3") << "x";
    const auto tracks = ScanMusicFolder(root);
    std::vector<std::string> names;
    for (const auto& track : tracks) names.push_back(track.folder + "|" + track.name);
    fs::remove_all(root, ignored);
    const std::vector<std::string> expected{"|2 Two", "|10 Ten", "Album A|3 Third", "Album A/CD 2|1 Disc two", "Album B|01 First",
        "M\xC3\xB6tley Cr\xC3\xBC" "e|Kickstart \xC3\xBC"};
    std::string found;
    for (const auto& name : names) found += " [" + name + "]";
    Check(names == expected, "The music folder is read in the wrong order or incompletely:" + found);
    Check(tracks.size() == expected.size() && tracks[1].path.find("10 Ten.mp3") != std::string::npos, "A track has the wrong path");
    Check(ScanMusicFolder(root).empty(), "A missing music folder gave tracks");
}

// "Now playing" of a radio station and the play times.
void TestStreamText() {
    Check(IcyStreamTitle("StreamTitle='Queen - Bohemian Rhapsody';StreamUrl='';") == "Queen - Bohemian Rhapsody", "The ICY title was not read");
    Check(IcyStreamTitle("StreamTitle='Guns N' Roses - Paradise City';") == "Guns N' Roses - Paradise City", "An apostrophe ended the title");
    Check(IcyStreamTitle("StreamTitle='  Spaced  ';") == "Spaced" && IcyStreamTitle("StreamTitle=' - ';").empty() && IcyStreamTitle("StreamTitle='';").empty() &&
        IcyStreamTitle("StreamUrl='x';").empty() && IcyStreamTitle("").empty(), "Empty or odd titles are wrong");
    Check(IcyStreamTitle("StreamTitle='Caf\xE9';") == "Caf\xC3\xA9" && IcyStreamTitle("StreamTitle='Caf\xC3\xA9';") == "Caf\xC3\xA9",
        "A Latin-1 title was not converted, or a UTF-8 one was");
    Check(IsValidUtf8("abc \xC3\xA4\xE2\x82\xAC\xF0\x9F\x8E\xB5") && !IsValidUtf8("\xC3") && !IsValidUtf8("\xE9t\xE9") && !IsValidUtf8("\xC3\x28"), "UTF-8 check is wrong");
    Check(FormatPlayTime(0) == "0:00" && FormatPlayTime(7.9) == "0:07" && FormatPlayTime(187.4) == "3:07" && FormatPlayTime(3725) == "1:02:05" &&
        FormatPlayTime(-3) == "0:00" && FormatPlayTime(std::nan("")) == "0:00", "Play times are formatted wrongly");
}
}

// The music folder and the texts of radio streams.
void RunMediaTests()
{
    TestMusicLibrary();
    TestStreamText();
}
