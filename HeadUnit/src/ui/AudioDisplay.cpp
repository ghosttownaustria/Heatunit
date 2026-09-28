#include "ui/AudioDisplay.h"
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace headunit {
namespace {
constexpr int kMeterSegments = 20;
// How much of a peak is left at the next look: peaks fall back slowly, like a real level meter.
constexpr float kPeakFallback = 0.82f;
}

// A readout of fixed height.
AudioDisplay::AudioDisplay(QWidget* parent) : QWidget(parent)
{
    setFixedHeight(176);
    setMinimumWidth(240);
}

// Shows the volume, the mute state and the streams' levels of the last look.
void AudioDisplay::SetState(int volume, bool isMuted, const std::array<AudioState::Meter, kAudioKindCount>& meters)
{
    m_volume = volume;
    m_isMuted = isMuted;
    for (std::size_t index = 0; index < meters.size(); ++index) {
        m_levels[index] = std::max(meters[index].peak, m_levels[index] * kPeakFallback);
        m_isActive[index] = meters[index].isActive;
    }
    update();
}

// The readout's size.
QSize AudioDisplay::sizeHint() const
{
    return {260, 176};
}

// Draws the frame, the volume and the level meters.
void AudioDisplay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor(74, 84, 100), 1));
    painter.setBrush(QColor(8, 14, 22));
    painter.drawRoundedRect(box, 8, 8);
    QFont font = painter.font();
    font.setPointSize(8);
    PaintVolume(painter, box, font);
    PaintMeters(painter, box, font);
}

// The volume: its label (with STUMM when muted), the number and a segmented bar, one segment per step.
void AudioDisplay::PaintVolume(QPainter& painter, const QRectF& box, const QFont& font) const
{
    const QColor accent = m_isMuted ? QColor(90, 100, 112) : QColor(64, 208, 255);
    painter.setFont(font);
    painter.setPen(QColor(120, 134, 152));
    painter.drawText(QRectF(box.left() + 12, box.top() + 8, 120, 14), Qt::AlignLeft | Qt::AlignVCenter, "LAUTSTAERKE");
    if (m_isMuted) {
        painter.setPen(QColor(255, 96, 96));
        painter.drawText(QRectF(box.right() - 92, box.top() + 8, 80, 14), Qt::AlignRight | Qt::AlignVCenter, "STUMM");
    }
    QFont big = font;
    big.setPointSize(24);
    big.setBold(true);
    painter.setFont(big);
    painter.setPen(accent);
    painter.drawText(QRectF(box.left() + 12, box.top() + 22, 60, 38), Qt::AlignLeft | Qt::AlignVCenter, QString::number(m_volume));
    const double barLeft = box.left() + 74;
    const double segment = (box.width() - 86) / AudioState::kMaxVolume;
    painter.setPen(Qt::NoPen);
    for (int step = 0; step < AudioState::kMaxVolume; ++step) {
        painter.setBrush(step < m_volume ? accent : QColor(26, 36, 50));
        const double height = 8 + 20.0 * (step + 1) / AudioState::kMaxVolume;
        painter.drawRect(QRectF(barLeft + step * segment, box.top() + 56 - height, std::max(1.0, segment - 1.5), height));
    }
}

// One row per stream: its name, an activity light and a meter of 20 segments (the square root makes quiet audio visible).
void AudioDisplay::PaintMeters(QPainter& painter, const QRectF& box, const QFont& font) const
{
    static const char* const kNames[kAudioKindCount] = {"MEDIEN", "NAVI", "SYSTEM"};
    painter.setFont(font);
    for (int stream = 0; stream < kAudioKindCount; ++stream) {
        const auto index = static_cast<std::size_t>(stream);
        const double top = box.top() + 72 + stream * 32;
        painter.setPen(m_isActive[index] ? QColor(214, 224, 238) : QColor(112, 124, 140));
        painter.drawText(QRectF(box.left() + 12, top, 56, 20), Qt::AlignLeft | Qt::AlignVCenter, kNames[index]);
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_isActive[index] ? QColor(72, 220, 110) : QColor(38, 48, 62));
        painter.drawEllipse(QPointF(box.left() + 76, top + 10), 4, 4);
        const double left = box.left() + 88;
        const double cell = (box.width() - 100) / kMeterSegments;
        const int lit = static_cast<int>(std::lround(std::sqrt(std::clamp(m_levels[index], 0.0f, 1.0f)) * kMeterSegments));
        for (int segment = 0; segment < kMeterSegments; ++segment) {
            const QColor on = segment < 13 ? QColor(72, 220, 110) : segment < 17 ? QColor(240, 200, 60) : QColor(240, 80, 70);
            painter.setBrush(segment < lit ? on : QColor(24, 33, 46));
            painter.drawRect(QRectF(left + segment * cell, top + 4, std::max(1.0, cell - 1.5), 12));
        }
    }
}
}
