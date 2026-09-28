#pragma once
#include "androidauto/DisplayConfig.h"
#include "androidauto/InputEvents.h"
#include "video/VideoDecoder.h"
#include <QElapsedTimer>
#include <QImage>
#include <QString>
#include <QWidget>
#include <functional>

namespace headunit {
// Shows the phone's picture (aspect ratio kept) and turns mouse input on it into touch events.
class VideoWidget final : public QWidget {
public:
    explicit VideoWidget(QWidget* parent = nullptr);

    void SetTouchHandler(std::function<void(TouchAction, int, int)> handler);
    void SetFrame(const VideoFrame& frame);
    void ClearFrame(const QString& message);
    bool HasFrame() const;
    const QImage& Image() const;
    void SetDisplay(const DisplayConfig& display);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    std::function<void(TouchAction, int, int)> m_onTouch;
    QImage m_image;
    QString m_message;
    DisplayConfig m_display{kDefaultDisplay};
    bool m_isTouching{};
    QElapsedTimer m_moveClock;   // paces move events while a finger is down

    void PaintEmptyScreen(QPainter& painter);
    bool Report(TouchAction action, const QPointF& position, bool isClamped);
};
}
