#include "ui/HomeTileSetup.h"
#include <algorithm>

namespace headunit {
namespace {
// Whether the setup lists `entry` already.
bool HasTile(const HomeTileSetup& setup, HomeMenuEntry entry)
{
    return std::any_of(setup.tiles.begin(), setup.tiles.end(), [entry](const HomeTileSetup::Tile& tile) { return tile.entry == entry; });
}

// `text` without spaces at either end.
std::string_view Trimmed(std::string_view text)
{
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}
}

// Every tile, shown, in the order a new radio has them.
HomeTileSetup DefaultTileSetup()
{
    HomeTileSetup setup;
    for (const HomeMenuEntry entry : kHomeMenuEntries) setup.tiles.push_back({entry, true});
    return setup;
}

// The tiles the home menu shows, in their order.
std::vector<HomeMenuEntry> ShownTiles(const HomeTileSetup& setup)
{
    std::vector<HomeMenuEntry> shown;
    for (const auto& tile : setup.tiles) {
        if (tile.isShown) shown.push_back(tile.entry);
    }
    return shown;
}

// Shows or hides a tile; false when nothing changed (Settings cannot be hidden).
bool SetTileShown(HomeTileSetup& setup, HomeMenuEntry entry, bool isShown)
{
    if (entry == HomeMenuEntry::Settings && !isShown) return false;
    for (auto& tile : setup.tiles) {
        if (tile.entry != entry || tile.isShown == isShown) continue;
        tile.isShown = isShown;
        return true;
    }
    return false;
}

// Moves `entry` one place along the order: `direction` -1 towards the front, +1 towards the back. `isAmongShown` (the
// home menu, which does not show hidden tiles): it passes the next shown tile, and any hidden ones on the way; otherwise
// (the settings list, which shows them all) its direct neighbour. False at an end.
bool MoveTile(HomeTileSetup& setup, HomeMenuEntry entry, int direction, bool isAmongShown)
{
    auto& tiles = setup.tiles;
    const auto found = std::find_if(tiles.begin(), tiles.end(), [entry](const HomeTileSetup::Tile& tile) { return tile.entry == entry; });
    if (found == tiles.end() || direction == 0) return false;
    const int count = static_cast<int>(tiles.size());
    const int step = direction > 0 ? 1 : -1;
    const int index = static_cast<int>(found - tiles.begin());
    int other = index + step;
    while (isAmongShown && other >= 0 && other < count && !tiles[static_cast<std::size_t>(other)].isShown) other += step;
    if (other < 0 || other >= count) return false;
    const HomeTileSetup::Tile moved = *found;
    tiles.erase(found);
    tiles.insert(tiles.begin() + other, moved);
    return true;
}

// The setup as text for the settings store: the tiles' names in their order, hidden ones marked with "-"
// ("AndroidAuto,Radio,-Vehicle,...").
std::string TileSetupText(const HomeTileSetup& setup)
{
    std::string text;
    for (const auto& tile : setup.tiles) {
        if (!text.empty()) text += ',';
        if (!tile.isShown) text += '-';
        text += HomeMenuId(tile.entry);
    }
    return text;
}

// Reads the text back. Unknown and repeated names are skipped; tiles the text does not name (new in this version) are
// added at the end, shown; Settings is shown whatever the text says. An empty text gives the default setup.
HomeTileSetup ParseTileSetup(std::string_view text)
{
    HomeTileSetup setup;
    while (!text.empty()) {
        const std::size_t comma = text.find(',');
        std::string_view name = Trimmed(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
        const bool isShown = name.empty() || name.front() != '-';
        if (!isShown) name.remove_prefix(1);
        for (const HomeMenuEntry entry : kHomeMenuEntries) {
            if (name == HomeMenuId(entry) && !HasTile(setup, entry)) setup.tiles.push_back({entry, isShown || entry == HomeMenuEntry::Settings});
        }
    }
    for (const HomeMenuEntry entry : kHomeMenuEntries) {
        if (!HasTile(setup, entry)) setup.tiles.push_back({entry, true});
    }
    return setup;
}
}
