#pragma once
#include "ui/HomeMenuLayout.h"
#include "ui/MenuPage.h"
#include <functional>
#include <optional>

class QVariantAnimation;

namespace headunit {
// The radio's home menu (layout in HomeMenuLayout.h, look from docs/design/home-menu.svg): the row of tiles. Turning
// the knob or the left and right arrows move the focus, pushing it opens the focused tile; a click on a tile opens
// that one.
class HomeMenu final : public MenuPage {
public:
    explicit HomeMenu(QWidget* parent = nullptr);
    // Moves the focus by `steps` tiles (positive: to the right) and scrolls the row to show it.
    void Move(int steps);
    // Opens the focused tile.
    void Activate();
    HomeMenuEntry Focused() const { return kHomeMenuEntries[m_focus]; }
    std::function<void(HomeMenuEntry)> onOpen;
    void Turn(int steps) override { Move(steps); }
    void Nudge(unsigned keycode) override;
    void Push() override { Activate(); }
protected:
    void Paint(QPainter& painter) override;
    void DisplayChanged() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    std::optional<int> TileAt(const QPointF& position) const;
    void Focus(int index);
    int m_focus{};
    std::optional<int> m_pressedTile;   // lit while the mouse is down on it
    double m_scroll{};                  // units; slides towards m_targetScroll
    double m_targetScroll{};
    QVariantAnimation* m_scrollAnimation{};
};
}
