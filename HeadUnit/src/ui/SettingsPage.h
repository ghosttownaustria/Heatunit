#pragma once
#include "ui/HomeTileSetup.h"
#include "ui/MenuPage.h"
#include "ui/TouchDrag.h"
#include <functional>
#include <optional>

namespace headunit {
// The radio's settings: which tiles the home menu shows, and in which order. Every tile is listed in the menu's order
// with a tick box. Turning the knob moves through the list, pushing it shows or hides the tile (Settings always stays),
// the up and down arrows move the tile one place up or down the order (as left and right do on the home menu). A tap
// on a row ticks it; dragging a row up or down moves its tile along the order with the finger.
class SettingsPage final : public MenuPage {
public:
    explicit SettingsPage(QWidget* parent = nullptr);

    void SetChangeHandler(std::function<void(const HomeTileSetup&)> handler);
    void SetSetup(const HomeTileSetup& setup);
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;

protected:
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::function<void(const HomeTileSetup&)> m_onChange;
    HomeTileSetup m_setup{DefaultTileSetup()};
    int m_focus{};
    std::optional<int> m_pressed;
    TouchDrag m_drag;
    std::optional<int> m_dragRow;   // where the dragged tile is now
    int m_dragStartRow{};

    int Count() const;
    QRectF RowRect(int index) const;
    std::optional<int> RowAt(const QPointF& position) const;
    void Toggle(int index);
};
}
