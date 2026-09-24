#pragma once
#include "androidauto/DisplayConfig.h"
#include "ui/HomeMenuLayout.h"
#include <QString>
#include <QWidget>
#include <functional>
#include <optional>

class QVariantAnimation;

namespace headunit {
// The radio's home menu (layout in HomeMenuLayout.h, look from docs/design/home-menu.svg). It takes the place of the
// phone's picture: a screen of the chosen display's shape with the clock and the row of tiles. The rotary knob moves
// the focus (Move) and opens the focused tile (Activate); a click on a tile opens that one.
class HomeMenu final : public QWidget {
public:
    explicit HomeMenu(QWidget* parent = nullptr);
    void SetDisplay(const DisplayConfig& display);
    // Moves the focus by `steps` tiles (positive: to the right) and scrolls the row to show it.
    void Move(int steps);
    // Opens the focused tile.
    void Activate();
    HomeMenuEntry Focused() const { return kHomeMenuEntries[m_focus]; }
    std::function<void(HomeMenuEntry)> onOpen;
    QSize sizeHint() const override { return {800, 480}; }
    QSize minimumSizeHint() const override { return {400, 240}; }
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
private:
    QRectF ScreenRect() const;
    std::optional<int> TileAt(const QPointF& position) const;
    void Focus(int index);
    void UpdateClock();
    DisplayConfig m_display{kDefaultDisplay};
    int m_focus{};
    std::optional<int> m_pressedTile;   // lit while the mouse is down on it
    double m_scroll{};                  // units; slides towards m_targetScroll
    double m_targetScroll{};
    QVariantAnimation* m_scrollAnimation{};
    QString m_clock;
};
}
