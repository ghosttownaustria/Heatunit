#include "ui/MenuPage.h"
#include "ui/HomeMenuLayout.h"
#include "ui/MenuStyle.h"
#include <QPainter>
#include <QTime>
#include <QTimer>

namespace headunit {
namespace {
const QPointF kClockPosition(12.249, 62.337);   // start of the baseline
}
MenuPage::MenuPage(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    auto* clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, &MenuPage::UpdateClock);
    clock->start(1000);
    UpdateClock();
}
void MenuPage::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
    DisplayChanged();
    update();
}
double MenuPage::Width() const { return HomeMenuWidth(m_display); }
void MenuPage::UpdateClock()
{
    const QString now = QTime::currentTime().toString("HH:mm");
    if (now == m_clock) return;
    m_clock = now;
    update();
}
// Where the display is drawn: its shape, as large as the widget allows and centred, like the phone's picture.
QRectF MenuPage::ScreenRect() const
{
    const QSizeF shown = QSizeF(m_display.width, m_display.height).scaled(QSizeF(size()), Qt::KeepAspectRatio);
    return QRectF(QPointF((width() - shown.width()) / 2, (height() - shown.height()) / 2), shown);
}
std::optional<QPointF> MenuPage::ToDesign(const QPointF& position) const
{
    const QRectF screen = ScreenRect();
    if (screen.isEmpty() || !screen.contains(position)) return std::nullopt;
    const double scale = screen.height() / kHomeMenuHeight;
    return (position - screen.topLeft()) / scale;
}
void MenuPage::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(17, 17, 17));   // around the screen, as around the phone's picture
    const QRectF screen = ScreenRect();
    if (screen.isEmpty()) return;
    painter.fillRect(screen, Qt::black);
    painter.setClipRect(screen);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.translate(screen.topLeft());
    const double scale = screen.height() / kHomeMenuHeight;
    painter.scale(scale, scale);
    painter.setFont(menu::Font(menu::kFontSize));
    painter.setPen(menu::kText);
    painter.drawText(kClockPosition, m_clock);
    Paint(painter);
}
}
