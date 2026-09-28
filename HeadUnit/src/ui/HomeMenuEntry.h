#pragma once
#include "androidauto/ConsoleController.h"
#include <iterator>
#include <optional>

namespace headunit {
// The tiles of the radio's home menu.
enum class HomeMenuEntry { AndroidAuto, Multimedia, Radio, Telephone, Navigation, Vehicle, Settings };

// Every tile there is, in the order a new radio shows them.
inline constexpr HomeMenuEntry kHomeMenuEntries[] = {HomeMenuEntry::AndroidAuto, HomeMenuEntry::Multimedia, HomeMenuEntry::Radio,
    HomeMenuEntry::Telephone, HomeMenuEntry::Navigation, HomeMenuEntry::Vehicle, HomeMenuEntry::Settings};
inline constexpr int kHomeMenuCount = static_cast<int>(std::size(kHomeMenuEntries));

// The title a tile shows.
constexpr const char* HomeMenuTitle(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return "Android Auto";
    case HomeMenuEntry::Multimedia: return "Multimedia";
    case HomeMenuEntry::Radio: return "Radio";
    case HomeMenuEntry::Telephone: return "Telephone";
    case HomeMenuEntry::Navigation: return "Navigation";
    case HomeMenuEntry::Vehicle: return "Vehicle";
    case HomeMenuEntry::Settings: return "Settings";
    }
    return "";
}

// The name a tile is remembered by (TileSetupText).
constexpr const char* HomeMenuId(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return "AndroidAuto";
    default: return HomeMenuTitle(entry);
    }
}

// The radio's own page a tile opens: its music player, its tuner and its settings.
constexpr std::optional<ConsoleController::Screen> HomeMenuPage(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::Multimedia: return ConsoleController::Screen::Multimedia;
    case HomeMenuEntry::Radio: return ConsoleController::Screen::Radio;
    case HomeMenuEntry::Settings: return ConsoleController::Screen::Settings;
    default: return std::nullopt;
    }
}

// The controller key the phone's tiles stand for: opening the tile is pressing that key (Android Auto connects or brings
// the phone to the front, as the projection key does). Vehicle has neither a page nor a key yet.
constexpr std::optional<ConsoleKey> HomeMenuKey(HomeMenuEntry entry)
{
    switch (entry) {
    case HomeMenuEntry::AndroidAuto: return ConsoleKey::Projection;
    case HomeMenuEntry::Telephone: return ConsoleKey::Tel;
    case HomeMenuEntry::Navigation: return ConsoleKey::Nav;
    default: return std::nullopt;
    }
}
}
