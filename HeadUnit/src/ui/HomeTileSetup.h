#pragma once
#include "ui/HomeMenuEntry.h"
#include <string>
#include <string_view>
#include <vector>

namespace headunit {
// Which tiles the home menu shows, and in which order: every tile once, each shown or hidden. Settings is always shown,
// it is the way back to the others.
struct HomeTileSetup {
    // One tile of the order.
    struct Tile {
        friend bool operator==(const Tile&, const Tile&) = default;

        HomeMenuEntry entry{};
        bool isShown{true};
    };

    friend bool operator==(const HomeTileSetup&, const HomeTileSetup&) = default;

    std::vector<Tile> tiles;
};

HomeTileSetup DefaultTileSetup();
std::vector<HomeMenuEntry> ShownTiles(const HomeTileSetup& setup);
bool SetTileShown(HomeTileSetup& setup, HomeMenuEntry entry, bool isShown);
bool MoveTile(HomeTileSetup& setup, HomeMenuEntry entry, int direction, bool isAmongShown);
std::string TileSetupText(const HomeTileSetup& setup);
HomeTileSetup ParseTileSetup(std::string_view text);
}
