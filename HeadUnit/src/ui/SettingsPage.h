#pragma once
#include "ui/HomeMenuLayout.h"
#include "ui/MenuPage.h"
#include <functional>
#include <optional>

namespace headunit {
// The radio's settings: which tiles the home menu shows, and in which order. Every tile is listed in the menu's order
// with a tick box. Turning the knob moves through the list, pushing it shows or hides the tile (Settings always stays),
// the up and down arrows move the tile one place up or down the order (as left and right do on the home menu). A click
// on a row ticks it.
class SettingsPage final : public MenuPage {
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void SetSetup(const HomeTileSetup& setup);
    // A change the user made; the window stores it and hands it back with SetSetup.
    std::function<void(const HomeTileSetup&)> onChange;
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;
protected:
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    int Count() const { return static_cast<int>(m_setup.tiles.size()); }
    QRectF RowRect(int index) const;
    std::optional<int> RowAt(const QPointF& position) const;
    void Toggle(int index);
    HomeTileSetup m_setup{DefaultTileSetup()};
    int m_focus{};
    std::optional<int> m_pressed;
};
}
