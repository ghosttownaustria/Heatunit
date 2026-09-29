#include "ui/PlayerPage.h"
#include "androidauto/ProjectionKeys.h"
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>
#include <QWheelEvent>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
using State = AudioPlayer::State;
using Part = PageFocus::Part;
// Layout in design units (the screen is 600 high): the tile as on the home menu, everything else in the column at its
// right (see menu::kPageTile).
constexpr double kHeaderTop = 24, kHeaderHeight = 50, kHeaderGap = 12, kHeaderMinWidth = 140, kHeaderMaxWidth = 360;
constexpr double kSubtitleBaseline = 168;
constexpr double kProgressTop = 182, kProgressHeight = 5;
constexpr double kControlsTop = 200, kControlHeight = 52, kControlWidth = 84, kControlGap = 12;
constexpr int kControlCount = 3;   // previous, play, next
constexpr double kListTop = 268, kRowHeight = 46, kScrollbarRoom = 14;
constexpr int kVisibleRows = 5;
constexpr int kWheelStep = 120;
constexpr int kLookIntervalMs = 250;
}

// A page of `entry` that plays through `player`. The player runs on its own thread; the page looks at it a few times a
// second (progress, "now playing", the end of a track).
PlayerPage::PlayerPage(HomeMenuEntry entry, AudioPlayer& player, QWidget* parent) : MenuPage(parent), m_entry(entry), m_player(player)
{
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PlayerPage::Look);
    timer->start(kLookIntervalMs);
}

// Who hears that this page is about to start or resume the player (the window pauses the phone's music then).
void PlayerPage::SetWillPlayHandler(std::function<void()> handler)
{
    m_onWillPlay = std::move(handler);
}

// Turning moves the focus within its part.
void PlayerPage::Turn(int steps)
{
    Refocus(TurnFocus(m_focus, Shape(), steps));
}

// Left and right skip (they never move the focus; turning does that); up and down move between the parts.
void PlayerPage::Nudge(unsigned keycode)
{
    if (keycode == keys::DpadLeft || keycode == keys::DpadRight) {
        Skip(keycode == keys::DpadLeft ? -1 : +1);
        Look();
        return;
    }
    Refocus(NudgeFocus(m_focus, Shape(), keycode));
}

// Pushing activates what the focus is on.
void PlayerPage::Push()
{
    Refocus(FitFocus(m_focus, Shape()));
    Activate(m_focus.part, m_focus.part == Part::List ? m_focus.row : m_focus.button);
}

// A media key (keys::MediaPlayPause, MediaNext, ...) while this page's source plays or is paused: handled here and true.
// False when the player is not this page's (the key then belongs to the phone).
bool PlayerPage::MediaKey(unsigned keycode)
{
    if (!IsOwnActive()) return false;
    const bool isPaused = m_player.CurrentStatus().state == State::Paused;
    switch (keycode) {
    case keys::MediaPlayPause: PlayPause(); break;
    case keys::MediaPlay: if (isPaused) PlayPause(); break;
    case keys::MediaPause: if (!isPaused) PlayPause(); break;
    case keys::MediaStop: m_player.Stop(); break;
    case keys::MediaNext: Next(); break;
    case keys::MediaPrevious: Previous(); break;
    default: return false;
    }
    Look();
    return true;
}

// Another source (the phone's music) has started: this page's player falls silent if it plays. The radio stops; the
// music player pauses where it is.
void PlayerPage::GiveWay()
{
    if (IsOwnSoundingNow()) m_player.Stop();
}

// 0..1; negative for no progress bar.
double PlayerPage::Progress() const
{
    return -1;
}

// Left and right: the previous or next title (station).
void PlayerPage::Skip(int direction)
{
    if (direction < 0) Previous();
    else Next();
}

// Called about four times a second while the player is this page's (to go on after the end of a track).
void PlayerPage::OwnStatus(const AudioPlayer::Status&)
{
}

// The shared player.
AudioPlayer& PlayerPage::Player()
{
    return m_player;
}

// The player's state as of the last look.
const AudioPlayer::Status& PlayerPage::LastStatus() const
{
    return m_status;
}

// Whether the last look saw this page's source in the player.
bool PlayerPage::IsOwn() const
{
    return m_generation != 0 && m_status.generation == m_generation;
}

// Whether this page's source is opening, playing or paused, asked of the player now.
bool PlayerPage::IsOwnActive() const
{
    const AudioPlayer::Status status = m_player.CurrentStatus();
    return m_generation != 0 && status.generation == m_generation &&
        (status.state == State::Opening || status.state == State::Playing || status.state == State::Paused);
}

// Whether this page's source is opening or playing, asked of the player now.
bool PlayerPage::IsOwnSoundingNow() const
{
    const AudioPlayer::Status status = m_player.CurrentStatus();
    return m_generation != 0 && status.generation == m_generation && (status.state == State::Opening || status.state == State::Playing);
}

// Whether this page's source was opening or playing at the last look.
bool PlayerPage::IsOwnSounding() const
{
    return IsOwn() && (m_status.state == State::Opening || m_status.state == State::Playing);
}

// Starts `source`; the player is this page's from now on.
void PlayerPage::StartPlayer(const std::string& source)
{
    NotifyWillPlay();
    m_player.Play(source);
    m_status = m_player.CurrentStatus();
    m_generation = m_status.generation;
    update();
}

// What the status bar names while this page's source sounds (the station, the title); empty otherwise.
QString PlayerPage::SoundingName() const
{
    return IsOwnSoundingNow() ? NowTitle() : QString();
}

// Tells the window that this page is about to start or resume the player.
void PlayerPage::NotifyWillPlay()
{
    if (m_onWillPlay) m_onWillPlay();
}

// Puts the controller on a row of the list; a row out of view is brought to the middle of it.
void PlayerPage::FocusRow(int row)
{
    PageFocus focus = m_focus;
    focus.part = Part::List;
    focus.row = row;
    focus = FitFocus(focus, Shape());
    if (focus.row < m_firstRow || focus.row >= m_firstRow + kVisibleRows)
        m_firstRow = std::clamp(focus.row - kVisibleRows / 2, 0, std::max(0, RowCount() - kVisibleRows));
    Refocus(focus);
}

// Puts the controller on a button at the top.
void PlayerPage::FocusHeader(int index)
{
    PageFocus focus = m_focus;
    focus.part = Part::Header;
    focus.button = index;
    Refocus(FitFocus(focus, Shape()));
}

// After the list changed: the focus and the scrolling made valid again.
void PlayerPage::ListChanged()
{
    Refocus(FitFocus(m_focus, Shape()));
}

// The row of the list the focus is on (kept while the focus is elsewhere).
int PlayerPage::FocusedRow() const
{
    return m_focus.row;
}

// Draws the tile, the buttons at the top, what plays now, the controls and the list.
void PlayerPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kPageTile, m_entry, IsOwnSounding());
    const QStringList header = HeaderButtons();
    for (int index = 0; index < static_cast<int>(header.size()); ++index)
        DrawTextButton(painter, HeaderRect(index), header[index], m_focus.part == Part::Header && m_focus.button == index);
    PaintNowPlaying(painter);
    PaintControls(painter);
    PaintList(painter);
}

// Remembers what a click starts on.
void PlayerPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = HitAt(event->position());
}

// Like a button: it acts when the click also ends on it, and the controller's focus goes there.
void PlayerPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    if (!pressed || pressed != HitAt(event->position())) return;
    if (!pressed->isTile) {
        PageFocus focus = m_focus;
        focus.part = pressed->part;
        (pressed->part == Part::List ? focus.row : focus.button) = pressed->index;
        Refocus(focus);
    }
    Activate(pressed->part, pressed->index);
}

// The wheel scrolls the list without moving the focus (the knob's wheel turns the knob instead).
void PlayerPage::wheelEvent(QWheelEvent* event)
{
    const int steps = event->angleDelta().y() / kWheelStep;
    m_firstRow = std::clamp(m_firstRow - steps, 0, std::max(0, RowCount() - kVisibleRows));
    update();
    event->accept();
}

// The number of buttons and rows right now.
PageShape PlayerPage::Shape() const
{
    return {static_cast<int>(HeaderButtons().size()), kControlCount, RowCount()};
}

// Activates a button of the header, a control or a row.
void PlayerPage::Activate(Part part, int index)
{
    switch (part) {
    case Part::Header: PressHeader(index); break;
    case Part::Controls:
        if (index == 0) Previous();
        else if (index == 1) PlayPause();
        else Next();
        break;
    case Part::List:
        if (index >= 0 && index < RowCount()) PressRow(index);
        break;
    }
    Look();
}

// Moves the focus, scrolling the list so that the focused row stays in view.
void PlayerPage::Refocus(PageFocus focus)
{
    m_focus = focus;
    m_firstRow = ListFirstRow(m_focus.row, m_firstRow, kVisibleRows, RowCount());
    update();
}

// Looks at the player: its state is noted, and the page reacts when the player is its own.
void PlayerPage::Look()
{
    m_status = m_player.CurrentStatus();
    if (IsOwn()) OwnStatus(m_status);
    if (isVisible()) update();
}

// The right edge of the column, as far from the screen's edge as the tile.
double PlayerPage::Right() const
{
    return Width() - menu::kPageTile.left();
}

// Where header button `index` is: at the top, right-aligned up to the status bar.
QRectF PlayerPage::HeaderRect(int index) const
{
    const QStringList buttons = HeaderButtons();
    const QFontMetricsF metrics(menu::Font(24));
    double right = StatusBarLeft(Width()) - kHeaderGap;
    for (int button = static_cast<int>(buttons.size()) - 1; button >= 0; --button) {
        const double width = std::clamp(metrics.horizontalAdvance(buttons[button]) + 44, kHeaderMinWidth, kHeaderMaxWidth);
        if (button == index) return QRectF(right - width, kHeaderTop, width, kHeaderHeight);
        right -= width + kHeaderGap;
    }
    return {};
}

// Where control `index` is (previous, play, next).
QRectF PlayerPage::ControlRect(int index) const
{
    return QRectF(menu::kPageColumnLeft + index * (kControlWidth + kControlGap), kControlsTop, kControlWidth, kControlHeight);
}

// Where the `visibleIndex`th row in view is; a scrollbar takes room at the right when the list is longer than the view.
QRectF PlayerPage::RowRect(int visibleIndex) const
{
    const double scrollbar = RowCount() > kVisibleRows ? kScrollbarRoom : 0;
    return QRectF(menu::kPageColumnLeft, kListTop + visibleIndex * kRowHeight, Right() - menu::kPageColumnLeft - scrollbar, kRowHeight);
}

// What a click at a widget position hits; the tile plays and pauses.
std::optional<PlayerPage::Hit> PlayerPage::HitAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    if (menu::kPageTile.contains(*point)) return Hit{Part::Controls, 1, true};
    for (int index = 0; index < static_cast<int>(HeaderButtons().size()); ++index) {
        if (HeaderRect(index).contains(*point)) return Hit{Part::Header, index, false};
    }
    for (int index = 0; index < kControlCount; ++index) {
        if (ControlRect(index).contains(*point)) return Hit{Part::Controls, index, false};
    }
    for (int index = 0; index < kVisibleRows && m_firstRow + index < RowCount(); ++index) {
        if (RowRect(index).contains(*point)) return Hit{Part::List, m_firstRow + index, false};
    }
    return std::nullopt;
}

// What plays now: title, subtitle with its info at the right, and the progress of a file.
void PlayerPage::PaintNowPlaying(QPainter& painter) const
{
    using namespace menu;
    const double right = Right();
    const double width = right - kPageColumnLeft;
    const QFont titleFont = Font(30);
    const QFont subtitleFont = Font(24);
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kPageColumnLeft, kPageTitleBaseline), Elided(NowTitle(), titleFont, width));
    const QString info = NowInfo();
    const double infoWidth = info.isEmpty() ? 0 : QFontMetricsF(subtitleFont).horizontalAdvance(info);
    painter.setFont(subtitleFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kPageColumnLeft, kSubtitleBaseline), Elided(NowSubtitle(), subtitleFont, width - infoWidth - (infoWidth > 0 ? 24 : 0)));
    if (!info.isEmpty()) {
        painter.setPen(IsOwnSounding() ? kOrange : kDim);
        painter.drawText(QPointF(right - infoWidth, kSubtitleBaseline), info);
    }
    if (const double progress = Progress(); progress >= 0) {
        painter.fillRect(QRectF(kPageColumnLeft, kProgressTop, width, kProgressHeight), kLine);
        painter.fillRect(QRectF(kPageColumnLeft, kProgressTop, width * std::clamp(progress, 0.0, 1.0), kProgressHeight), kOrange);
    }
}

// The three controls and the note at their right.
void PlayerPage::PaintControls(QPainter& painter) const
{
    using namespace menu;
    const Symbol symbols[kControlCount] = {Symbol::Previous, PlaySymbol(), Symbol::Next};
    for (int index = 0; index < kControlCount; ++index)
        DrawButton(painter, ControlRect(index), symbols[index], m_focus.part == Part::Controls && m_focus.button == index, index == 1 && IsOwnSounding());
    const QFont noteFont = Font(20);
    const double noteLeft = ControlRect(kControlCount - 1).right() + 24;
    const QRectF noteBox(noteLeft, kControlsTop, Right() - noteLeft, kControlHeight);
    painter.setFont(noteFont);
    painter.setPen(kDim);
    painter.drawText(noteBox, Qt::AlignRight | Qt::AlignVCenter, Elided(ControlsNote(), noteFont, noteBox.width()));
}

// The rows in view with a scrollbar when there are more; the empty text in place of an empty list.
void PlayerPage::PaintList(QPainter& painter) const
{
    using namespace menu;
    const int count = RowCount();
    if (count == 0) {
        painter.setFont(Font(24));
        painter.setPen(kDim);
        painter.drawText(QRectF(kPageColumnLeft, kListTop + 10, Right() - kPageColumnLeft, kVisibleRows * kRowHeight - 10),
            Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, EmptyText());
        return;
    }
    const int current = CurrentRow();
    for (int index = 0; index < kVisibleRows && m_firstRow + index < count; ++index) {
        const int row = m_firstRow + index;
        DrawRow(painter, RowRect(index), RowText(row), RowNote(row), m_focus.part == Part::List && m_focus.row == row, row == current);
    }
    if (count <= kVisibleRows) return;
    const QRectF track(Right() - 4, kListTop, 4, kVisibleRows * kRowHeight);
    painter.fillRect(track, kLine);
    const double thumb = std::max(24.0, track.height() * kVisibleRows / count);
    const double top = track.top() + (track.height() - thumb) * m_firstRow / std::max(1, count - kVisibleRows);
    painter.fillRect(QRectF(track.left(), top, track.width(), thumb), kDim);
}
}
