#pragma once
#include "audio/AudioState.h"
#include <QWidget>
#include <array>

namespace headunit {
// The car's audio readout: volume steps, mute and a level meter for each of the phone's streams.
class AudioDisplay final : public QWidget {
public:
    explicit AudioDisplay(QWidget* parent = nullptr);

    void SetState(int volume, bool isMuted, const std::array<AudioState::Meter, kAudioKindCount>& meters);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int m_volume{};
    bool m_isMuted{};
    std::array<float, kAudioKindCount> m_levels{};
    std::array<bool, kAudioKindCount> m_isActive{};

    void PaintVolume(QPainter& painter, const QRectF& box, const QFont& font) const;
    void PaintMeters(QPainter& painter, const QRectF& box, const QFont& font) const;
};
}
