#pragma once
#include "ui/HomeMenuLayout.h"
#include "ui/MenuPage.h"
#include <functional>
#include <optional>
#include <vector>

class QVariantAnimation;

namespace headunit {
// The radio's home menu (layout in HomeMenuLayout.h, look from docs/design/home-menu.svg): the row of tiles the user
// has chosen, the focused one in the middle, arrows at the edges where more tiles follow and the bar at the bottom.
// Turning the knob moves the focus, pushing it opens the focused tile; the left and right arrows move the focused tile
// itself one place along the row (the window stores the new order and hands the tiles back). A click on a tile opens
// it, a click on an edge arrow moves the focus that way.
class HomeMenu final : public MenuPage {
public:
    explicit HomeMenu(QWidget* parent = nullptr);
    // The tiles to show, in their order. The focus stays on the tile it was on while that tile is still shown.
    void SetTiles(std::vector<HomeMenuEntry> tiles);
    // Moves the focus by `steps` tiles (positive: to the right); the row slides to keep it in the middle.
    void Move(int steps);
    // Opens the focused tile.
    void Activate();
    HomeMenuEntry Focused() const { return m_tiles[static_cast<std::size_t>(m_focus)]; }
    std::function<void(HomeMenuEntry)> onOpen;
    // Asks to move a tile one place along the row: -1 to the left, +1 to the right.
    std::function<void(HomeMenuEntry, int direction)> onShift;
    void Turn(int steps) override { Move(steps); }
    void Nudge(unsigned keycode) override;
    void Push() override { Activate(); }
protected:
    void Paint(QPainter& painter) override;
    void DisplayChanged() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    // What is under the mouse: a tile (its index) or an edge arrow (-1 left, +1 right).
    struct Spot {
        std::optional<int> tile;
        int edge{};
        friend bool operator==(const Spot&, const Spot&) = default;
    };
    Spot SpotAt(const QPointF& position) const;
    int Count() const { return static_cast<int>(m_tiles.size()); }
    void Focus(int index);
    void DrawEdge(QPainter& painter, bool isRight) const;
    std::vector<HomeMenuEntry> m_tiles;
    int m_focus{};
    std::optional<Spot> m_pressed;      // a pressed tile is lit while the mouse is down on it
    double m_scroll{};                  // units; slides towards m_targetScroll
    double m_targetScroll{};
    QVariantAnimation* m_scrollAnimation{};
    int m_wheelCarry{};
};
}
