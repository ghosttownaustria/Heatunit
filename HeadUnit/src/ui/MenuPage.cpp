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

// A page with its clock, which updates once a minute (checked every second).
MenuPage::MenuPage(QWidget* parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setCursor(Qt::PointingHandCursor);
    auto* clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, &MenuPage::UpdateClock);
    clock->start(1000);
    UpdateClock();
}

// The display whose shape the page takes.
void MenuPage::SetDisplay(const DisplayConfig& display)
{
    m_display = display;
    DisplayChanged();
    update();
}

// Back while the page is in front; true when the page took it itself (closing a list it opened).
bool MenuPage::Back()
{
    return false;
}

// The phone's smallest display.
QSize MenuPage::sizeHint() const
{
    return {800, 480};
}

// Half the smallest display.
QSize MenuPage::minimumSizeHint() const
{
    return {400, 240};
}

// The display changed; a page whose layout depends on its width adjusts here.
void MenuPage::DisplayChanged()
{
}

// The display whose shape the page takes.
const DisplayConfig& MenuPage::Display() const
{
    return m_display;
}

// Width of the screen in design units.
double MenuPage::Width() const
{
    return HomeMenuWidth(m_display);
}

// A position in the widget as design units; none outside the screen.
std::optional<QPointF> MenuPage::ToDesign(const QPointF& position) const
{
    const QRectF screen = ScreenRect();
    if (screen.isEmpty() || !screen.contains(position)) return std::nullopt;
    const double scale = screen.height() / kHomeMenuHeight;
    return (position - screen.topLeft()) / scale;
}

// Draws the black screen with the clock, then the page on it.
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

// Where the display is drawn: its shape, as large as the widget allows and centred, like the phone's picture.
QRectF MenuPage::ScreenRect() const
{
    return menu::ScreenRectIn(QSizeF(size()), m_display);
}

// Redraws when the minute changed.
void MenuPage::UpdateClock()
{
    const QString now = QTime::currentTime().toString("HH:mm");
    if (now == m_clock) return;
    m_clock = now;
    update();
}
}
