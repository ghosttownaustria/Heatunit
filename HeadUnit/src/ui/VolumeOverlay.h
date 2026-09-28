#pragma once
#include "androidauto/DisplayConfig.h"
#include <QRectF>
#include <QWidget>
#include <functional>

class QTimer;

namespace headunit {
// The volume bar on the head unit's screen (layout in VolumeBar.h, look of the radio's pages): it appears over whatever
// is in front, one of the radio's pages or the phone's picture, whenever the volume or the mute state changes, and goes
// after a moment. Touching or dragging along it sets the volume. Only the panel itself takes input (a mask), so the
// rest of the screen keeps working while it shows.
class VolumeOverlay final : public QWidget {
public:
    explicit VolumeOverlay(QWidget* parent);

    void SetDisplay(const DisplayConfig& display);
    void SetVolumeHandler(std::function<void(int volume)> handler);
    void ShowVolume(int volume, bool isMuted);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::function<void(int volume)> m_onVolume;
    DisplayConfig m_display{kDefaultDisplay};
    int m_volume{};
    bool m_isMuted{};
    bool m_isDragging{};
    QTimer* m_hideTimer{};

    QRectF ScreenRect() const;
    double DesignWidth() const;
    QRectF PanelRect() const;
    void UpdateMask();
    void SetVolumeAt(const QPointF& position);
};
}
