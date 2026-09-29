#include "ui/MenuPage.h"
#include "ui/HomeMenuLayout.h"
#include "ui/MenuStyle.h"
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTime>
#include <QTimer>
#include <utility>

namespace headunit {
namespace {
const QPointF kClockPosition(12.249, 62.337);   // start of the baseline
}

// A page with its clock, which updates once a minute (checked every second). The page watches its own mouse events,
// so that the status bar gets its touches before the page.
MenuPage::MenuPage(QWidget* parent) : QWidget(parent)
{
    installEventFilter(this);
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

// What the status bar shows; redrawn when it changed.
void MenuPage::SetStatus(const StatusState& status)
{
    if (status == m_status) return;
    m_status = status;
    update();
}

// Who acts on a touch of the status bar's speaker, microphone or home.
void MenuPage::SetStatusHandler(std::function<void(StatusButton)> handler)
{
    m_onStatus = std::move(handler);
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
    PaintStatus(painter);
    Paint(painter);
}

// A press on the status bar's symbols is theirs: lit while the finger is on it, acting when it is lifted there. The
// page never sees it.
bool MenuPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != this) return QWidget::eventFilter(watched, event);
    const auto type = event->type();
    if (type != QEvent::MouseButtonPress && type != QEvent::MouseMove && type != QEvent::MouseButtonRelease && type != QEvent::MouseButtonDblClick)
        return QWidget::eventFilter(watched, event);
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
        if (mouse->button() != Qt::LeftButton) return false;
        m_statusPressed = StatusButtonUnder(mouse->position());
        if (m_statusPressed) update();
        return m_statusPressed.has_value();
    }
    if (!m_statusPressed) return false;
    if (type == QEvent::MouseButtonRelease && mouse->button() == Qt::LeftButton) {
        const auto pressed = std::exchange(m_statusPressed, std::nullopt);
        update();
        if (StatusButtonUnder(mouse->position()) == pressed && m_onStatus) m_onStatus(*pressed);
    }
    return true;
}

// Where the display is drawn: its shape, as large as the widget allows and centred, like the phone's picture.
QRectF MenuPage::ScreenRect() const
{
    return menu::ScreenRectIn(QSizeF(size()), m_display);
}

// The right side of the status bar: the sound's source (shortened to fit), the speaker, the microphone and home.
void MenuPage::PaintStatus(QPainter& painter) const
{
    const double width = Width();
    if (!m_status.source.empty()) {
        const QFont font = menu::Font(menu::kFontSize);
        const QString source = menu::Elided(QString::fromStdString(m_status.source), font, StatusSourceMaxWidth(width));
        painter.setFont(font);
        painter.setPen(menu::kText);
        painter.drawText(QPointF(StatusSourceRight(width) - QFontMetricsF(font).horizontalAdvance(source), kStatusBaseline), source);
    }
    const auto draw = [&](StatusButton button, menu::Icon icon, bool isStruck) {
        const StatusBox box = StatusIconBox(button, width);
        menu::DrawIcon(painter, icon, QRectF(box.left, box.top, box.width, box.height), isStruck, m_statusPressed == button);
    };
    draw(StatusButton::Speaker, menu::Icon::Speaker, m_status.isMuted);
    draw(StatusButton::Microphone, menu::Icon::Microphone, m_status.isMicrophoneMuted);
    draw(StatusButton::Home, menu::Icon::Home, false);
}

// The status bar's symbol at a widget position, if any.
std::optional<StatusButton> MenuPage::StatusButtonUnder(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    return StatusButtonAt(point->x(), point->y(), Width());
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
