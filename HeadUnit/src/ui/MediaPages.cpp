#include "ui/MediaPages.h"
#include "media/StreamText.h"
#include <QCollator>
#include <QDesktopServices>
#include <QDir>
#include <QFontMetricsF>
#include <QFutureWatcher>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace headunit {
namespace {
using State = AudioPlayer::State;
using Part = PageFocus::Part;
// Layout of a player page in design units (the screen is 600 high): the tile as on the home menu, everything else in
// the column at its right.
const QRectF kTile(25, 100, 250, 400);
constexpr double kLeft = 300;
constexpr double kHeaderTop = 24, kHeaderHeight = 50, kHeaderGap = 12, kHeaderMinWidth = 140, kHeaderMaxWidth = 420;
constexpr double kTitleBaseline = 132, kSubtitleBaseline = 168;
constexpr double kProgressTop = 182, kProgressHeight = 5;
constexpr double kControlsTop = 200, kControlHeight = 52, kControlWidth = 84, kControlGap = 12;
constexpr int kControlCount = 3;   // previous, play, next
constexpr double kListTop = 268, kRowHeight = 46, kScrollbarRoom = 14;
constexpr int kVisibleRows = 5;
constexpr int kWheelStep = 120;

// Remembered between runs, like the display size (QSettings: registry on Windows, ~/.config on Linux).
QSettings Settings() { return QSettings("HeadUnit", "HeadUnit"); }
constexpr const char* kCountrySetting = "radio/country";
constexpr const char* kStationSetting = "radio/station";

QString Text(const std::string& value) { return QString::fromStdString(value); }
QString CountryName(const QString& code)
{
    const QLocale::Territory territory = QLocale::codeToTerritory(code);
    return territory == QLocale::AnyTerritory ? code : QLocale::territoryToString(territory);
}
// The country the computer is set to (Windows region, LANG on Linux); Germany when it names none.
QString SystemCountry()
{
    const QString code = QLocale::territoryToCode(QLocale::system().territory());
    return code.size() == 2 ? code.toUpper() : QString("DE");
}
// The order a listener looks names up in: alphabetical as the user's language has it (umlauts with their letter),
// regardless of case, numbers by their value ("2" before "10").
bool NameLess(const QString& a, const QString& b)
{
    static const QCollator collator = [] {
        QCollator result;
        result.setCaseSensitivity(Qt::CaseInsensitive);
        result.setNumericMode(true);
        return result;
    }();
    return collator.compare(a, b) < 0;
}
}

// ---------------------------------------------------------------- PlayerPage

PlayerPage::PlayerPage(HomeMenuEntry entry, AudioPlayer& player, QWidget* parent) : MenuPage(parent), m_entry(entry), m_player(player)
{
    // The player runs on its own thread; the page looks at it a few times a second (progress, "now playing", the
    // end of a track).
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &PlayerPage::Look);
    timer->start(250);
}
void PlayerPage::Look()
{
    m_status = m_player.CurrentStatus();
    if (IsOwn()) OwnStatus(m_status);
    if (isVisible()) update();
}
bool PlayerPage::IsOwnSoundingNow() const
{
    const AudioPlayer::Status status = m_player.CurrentStatus();
    return m_generation != 0 && status.generation == m_generation && (status.state == State::Opening || status.state == State::Playing);
}
void PlayerPage::GiveWay()
{
    if (IsOwnSoundingNow()) m_player.Stop();
}
void PlayerPage::Skip(int direction)
{
    if (direction < 0) Previous();
    else Next();
}
bool PlayerPage::IsOwnActive() const
{
    const AudioPlayer::Status status = m_player.CurrentStatus();
    return m_generation != 0 && status.generation == m_generation &&
        (status.state == State::Opening || status.state == State::Playing || status.state == State::Paused);
}
void PlayerPage::StartPlayer(const std::string& source)
{
    if (onWillPlay) onWillPlay();
    m_player.Play(source);
    m_status = m_player.CurrentStatus();
    m_generation = m_status.generation;
    update();
}
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
PageShape PlayerPage::Shape() const { return {static_cast<int>(HeaderButtons().size()), kControlCount, RowCount()}; }
void PlayerPage::Refocus(PageFocus focus)
{
    m_focus = focus;
    m_firstRow = ListFirstRow(m_focus.row, m_firstRow, kVisibleRows, RowCount());
    update();
}
void PlayerPage::Turn(int steps) { Refocus(TurnFocus(m_focus, Shape(), steps)); }
void PlayerPage::Nudge(unsigned keycode)
{
    // Left and right skip; they never move the focus (turning does that).
    if (keycode == keys::DpadLeft || keycode == keys::DpadRight) {
        Skip(keycode == keys::DpadLeft ? -1 : +1);
        Look();
        return;
    }
    Refocus(NudgeFocus(m_focus, Shape(), keycode));
}
void PlayerPage::Push()
{
    Refocus(FitFocus(m_focus, Shape()));
    Activate(m_focus.part, m_focus.part == Part::List ? m_focus.row : m_focus.button);
}
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
void PlayerPage::FocusHeader(int index)
{
    PageFocus focus = m_focus;
    focus.part = Part::Header;
    focus.button = index;
    Refocus(FitFocus(focus, Shape()));
}
void PlayerPage::ListChanged() { Refocus(FitFocus(m_focus, Shape())); }
void PlayerPage::Activate(Part part, int index)
{
    switch (part) {
    case Part::Header: PressHeader(index); break;
    case Part::Controls:
        if (index == 0) Previous();
        else if (index == 1) PlayPause();
        else Next();
        break;
    case Part::List: if (index >= 0 && index < RowCount()) PressRow(index); break;
    }
    Look();
}

double PlayerPage::Right() const { return Width() - kTile.left(); }
QRectF PlayerPage::HeaderRect(int index) const
{
    // Right-aligned at the top, next to nothing but the clock.
    const QStringList buttons = HeaderButtons();
    const QFontMetricsF metrics(menu::Font(24));
    double right = Right();
    for (int i = static_cast<int>(buttons.size()) - 1; i >= 0; --i) {
        const double width = std::clamp(metrics.horizontalAdvance(buttons[i]) + 44, kHeaderMinWidth, kHeaderMaxWidth);
        if (i == index) return QRectF(right - width, kHeaderTop, width, kHeaderHeight);
        right -= width + kHeaderGap;
    }
    return {};
}
QRectF PlayerPage::ControlRect(int index) const
{
    return QRectF(kLeft + index * (kControlWidth + kControlGap), kControlsTop, kControlWidth, kControlHeight);
}
QRectF PlayerPage::RowRect(int visibleIndex) const
{
    const double scrollbar = RowCount() > kVisibleRows ? kScrollbarRoom : 0;
    return QRectF(kLeft, kListTop + visibleIndex * kRowHeight, Right() - kLeft - scrollbar, kRowHeight);
}
std::optional<PlayerPage::Hit> PlayerPage::HitAt(const QPointF& position) const
{
    const auto point = ToDesign(position);
    if (!point) return std::nullopt;
    if (kTile.contains(*point)) return Hit{Part::Controls, 1, true};   // the tile plays and pauses
    for (int i = 0; i < static_cast<int>(HeaderButtons().size()); ++i)
        if (HeaderRect(i).contains(*point)) return Hit{Part::Header, i, false};
    for (int i = 0; i < kControlCount; ++i)
        if (ControlRect(i).contains(*point)) return Hit{Part::Controls, i, false};
    for (int i = 0; i < kVisibleRows && m_firstRow + i < RowCount(); ++i)
        if (RowRect(i).contains(*point)) return Hit{Part::List, m_firstRow + i, false};
    return std::nullopt;
}
void PlayerPage::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) m_pressed = HitAt(event->position());
}
void PlayerPage::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) return;
    const auto pressed = std::exchange(m_pressed, std::nullopt);
    // Like a button: it acts when the click also ends on it, and the controller's focus goes there.
    if (!pressed || pressed != HitAt(event->position())) return;
    if (!pressed->isTile) {
        PageFocus focus = m_focus;
        focus.part = pressed->part;
        (pressed->part == Part::List ? focus.row : focus.button) = pressed->index;
        Refocus(focus);
    }
    Activate(pressed->part, pressed->index);
}
void PlayerPage::wheelEvent(QWheelEvent* event)
{
    // The wheel scrolls the list without moving the focus (the knob's wheel turns the knob instead).
    const int steps = event->angleDelta().y() / kWheelStep;
    m_firstRow = std::clamp(m_firstRow - steps, 0, std::max(0, RowCount() - kVisibleRows));
    update();
    event->accept();
}
void PlayerPage::Paint(QPainter& painter)
{
    using namespace menu;
    DrawTile(painter, kTile, m_entry, IsOwnSounding());
    const QStringList header = HeaderButtons();
    for (int i = 0; i < static_cast<int>(header.size()); ++i)
        DrawTextButton(painter, HeaderRect(i), header[i], m_focus.part == Part::Header && m_focus.button == i);

    // What plays now: title, subtitle with its info at the right, and the progress of a file.
    const double right = Right(), width = right - kLeft;
    const QFont titleFont = Font(30), subtitleFont = Font(24);
    painter.setFont(titleFont);
    painter.setPen(kText);
    painter.drawText(QPointF(kLeft, kTitleBaseline), Elided(NowTitle(), titleFont, width));
    const QString info = NowInfo();
    const double infoWidth = info.isEmpty() ? 0 : QFontMetricsF(subtitleFont).horizontalAdvance(info);
    painter.setFont(subtitleFont);
    painter.setPen(kDim);
    painter.drawText(QPointF(kLeft, kSubtitleBaseline), Elided(NowSubtitle(), subtitleFont, width - infoWidth - (infoWidth > 0 ? 24 : 0)));
    if (!info.isEmpty()) {
        painter.setPen(IsOwnSounding() ? kOrange : kDim);
        painter.drawText(QPointF(right - infoWidth, kSubtitleBaseline), info);
    }
    if (const double progress = Progress(); progress >= 0) {
        painter.fillRect(QRectF(kLeft, kProgressTop, width, kProgressHeight), kLine);
        painter.fillRect(QRectF(kLeft, kProgressTop, width * std::clamp(progress, 0.0, 1.0), kProgressHeight), kOrange);
    }

    const Symbol symbols[kControlCount] = {Symbol::Previous, PlaySymbol(), Symbol::Next};
    for (int i = 0; i < kControlCount; ++i)
        DrawButton(painter, ControlRect(i), symbols[i], m_focus.part == Part::Controls && m_focus.button == i, i == 1 && IsOwnSounding());
    const QFont noteFont = Font(20);
    const QRectF noteBox(ControlRect(kControlCount - 1).right() + 24, kControlsTop, right - ControlRect(kControlCount - 1).right() - 24, kControlHeight);
    painter.setFont(noteFont);
    painter.setPen(kDim);
    painter.drawText(noteBox, Qt::AlignRight | Qt::AlignVCenter, Elided(ControlsNote(), noteFont, noteBox.width()));

    const int count = RowCount();
    if (count == 0) {
        painter.setFont(Font(24));
        painter.setPen(kDim);
        painter.drawText(QRectF(kLeft, kListTop + 10, width, kVisibleRows * kRowHeight - 10), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, EmptyText());
        return;
    }
    const int current = CurrentRow();
    for (int i = 0; i < kVisibleRows && m_firstRow + i < count; ++i) {
        const int row = m_firstRow + i;
        DrawRow(painter, RowRect(i), RowText(row), RowNote(row), m_focus.part == Part::List && m_focus.row == row, row == current);
    }
    if (count > kVisibleRows) {
        const QRectF track(right - 4, kListTop, 4, kVisibleRows * kRowHeight);
        painter.fillRect(track, kLine);
        const double thumb = std::max(24.0, track.height() * kVisibleRows / count);
        const double top = track.top() + (track.height() - thumb) * m_firstRow / std::max(1, count - kVisibleRows);
        painter.fillRect(QRectF(track.left(), top, track.width(), thumb), kDim);
    }
}

// ---------------------------------------------------------------- MultimediaPage

MultimediaPage::MultimediaPage(AudioPlayer& player, QString folder, QWidget* parent)
    : PlayerPage(HomeMenuEntry::Multimedia, player, parent), m_folder(std::move(folder)), m_scan(new QFutureWatcher<std::vector<MusicTrack>>(this))
{
    QDir().mkpath(m_folder);
    connect(m_scan, &QFutureWatcher<std::vector<MusicTrack>>::finished, this, [this] {
        // The playing track stays the current one wherever it lands in the new list.
        const std::string playing = m_current >= 0 && m_current < RowCount() ? m_tracks[static_cast<std::size_t>(m_current)].path : std::string();
        m_tracks = m_scan->result();
        m_current = -1;
        for (int i = 0; i < RowCount(); ++i)
            if (m_tracks[static_cast<std::size_t>(i)].path == playing) m_current = i;
        ListChanged();
        update();
    });
    Rescan();
}
MultimediaPage::~MultimediaPage() { m_scan->waitForFinished(); }
void MultimediaPage::Rescan()
{
    if (m_scan->isRunning()) return;
    const QByteArray utf8 = m_folder.toUtf8();
    const std::filesystem::path folder(std::u8string(utf8.begin(), utf8.end()));
    m_scan->setFuture(QtConcurrent::run([folder] { return ScanMusicFolder(folder); }));
    update();
}
void MultimediaPage::showEvent(QShowEvent* event)
{
    PlayerPage::showEvent(event);
    Rescan();   // files may have been added since
}
void MultimediaPage::PressHeader(int index)
{
    if (index == 0) QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
    else Rescan();
}
QString MultimediaPage::RowText(int row) const { return Text(m_tracks[static_cast<std::size_t>(row)].name); }
QString MultimediaPage::RowNote(int row) const { return Text(m_tracks[static_cast<std::size_t>(row)].folder); }
QString MultimediaPage::EmptyText() const
{
    if (m_scan->isRunning()) return "Reading the music folder ...";
    return "No music yet. Copy music files (MP3, FLAC, M4A, OGG, OPUS, WAV) into\n" + QDir::toNativeSeparators(m_folder) +
        "\nthen choose Rescan. Sub folders (albums) are fine.";
}
QString MultimediaPage::NowTitle() const
{
    if (IsOwn() && !LastStatus().title.empty()) return Text(LastStatus().title);
    if (m_current >= 0) return RowText(m_current);
    return m_tracks.empty() ? QString("Music folder") : QString("Choose a title");
}
QString MultimediaPage::NowSubtitle() const
{
    if (IsOwn()) {
        const AudioPlayer::Status& status = LastStatus();
        if (status.state == State::Failed) return Text(status.error);
        if (status.state == State::Opening) return "Opening ...";
        if (!status.artist.empty()) return Text(status.artist);
    }
    if (m_current >= 0 && !m_tracks[static_cast<std::size_t>(m_current)].folder.empty()) return RowNote(m_current);
    return m_tracks.empty() ? QDir::toNativeSeparators(m_folder) : QString("Music folder");
}
QString MultimediaPage::NowInfo() const
{
    if (!IsOwn() || LastStatus().duration <= 0) return {};
    const AudioPlayer::Status& status = LastStatus();
    return (status.state == State::Paused ? QString("Paused · ") : QString()) + Text(FormatPlayTime(status.position)) + " / " + Text(FormatPlayTime(status.duration));
}
QString MultimediaPage::ControlsNote() const { return m_tracks.size() == 1 ? QString("1 title") : QString("%1 titles").arg(m_tracks.size()); }
double MultimediaPage::Progress() const
{
    if (!IsOwn() || LastStatus().duration <= 0) return -1;
    return LastStatus().position / LastStatus().duration;
}
menu::Symbol MultimediaPage::PlaySymbol() const { return IsOwnSounding() ? menu::Symbol::Pause : menu::Symbol::Play; }
void MultimediaPage::PlayTrack(int index)
{
    if (index < 0 || index >= RowCount()) return;
    m_current = index;
    StartPlayer(m_tracks[static_cast<std::size_t>(index)].path);
}
void MultimediaPage::Previous()
{
    if (m_tracks.empty()) return;
    // Like a car stereo: a few seconds into a title, back means its start.
    if (IsOwnActive() && m_current >= 0 && Player().CurrentStatus().position > 3) PlayTrack(m_current);
    else PlayTrack(std::max(0, m_current - 1));
}
void MultimediaPage::Next()
{
    if (m_current + 1 < RowCount()) PlayTrack(m_current + 1);
}
void MultimediaPage::PlayPause()
{
    if (IsOwnActive()) {
        const bool isPaused = Player().CurrentStatus().state == State::Paused;
        if (isPaused && onWillPlay) onWillPlay();   // resuming takes the sound back from the phone
        Player().SetPaused(!isPaused);
    } else if (!m_tracks.empty()) {
        PlayTrack(m_current >= 0 ? m_current : FocusedRow());
    }
}
void MultimediaPage::GiveWay()
{
    if (IsOwnSoundingNow()) Player().SetPaused(true);
}
void MultimediaPage::OwnStatus(const AudioPlayer::Status& status)
{
    // On to the next title at the end of one; a file that cannot be played is skipped, but not endlessly.
    if (status.state == State::Playing) m_failuresInARow = 0;
    else if (status.state == State::Ended) { m_failuresInARow = 0; Next(); }
    else if (status.state == State::Failed && ++m_failuresInARow < m_tracks.size()) Next();
}

// ---------------------------------------------------------------- RadioPage

RadioPage::RadioPage(AudioPlayer& player, QWidget* parent) : PlayerPage(HomeMenuEntry::Radio, player, parent), m_browser(new RadioBrowser(this))
{
    const QSettings settings = Settings();
    m_country = settings.value(kCountrySetting).toString().toUpper();
    if (m_country.size() != 2) m_country = SystemCountry();
    m_currentId = settings.value(kStationSetting).toString();
}
void RadioPage::showEvent(QShowEvent* event)
{
    PlayerPage::showEvent(event);
    // The directory is asked only once the tuner is opened, and again when it failed before.
    if (m_stations.empty() && !m_isLoadingStations) LoadStations();
}
void RadioPage::LoadStations()
{
    m_isLoadingStations = true;
    m_stationsError.clear();
    update();
    m_browser->FetchStations(m_country, [this](std::vector<RadioStation> stations, QString error) {
        m_isLoadingStations = false;
        m_stationsError = error;
        // The directory sends the most listened stations; the list shows them alphabetically (stations of the same name
        // keep their popularity order).
        std::stable_sort(stations.begin(), stations.end(), [](const RadioStation& a, const RadioStation& b) { return NameLess(a.name, b.name); });
        m_stations = std::move(stations);
        m_current = -1;
        for (int i = 0; i < static_cast<int>(m_stations.size()); ++i)
            if (m_stations[static_cast<std::size_t>(i)].id == m_currentId) m_current = i;
        m_stationRow = std::max(0, m_current);
        if (!m_isPicking) FocusRow(m_stationRow);
        update();
    });
}
void RadioPage::OpenCountries()
{
    m_isPicking = true;
    m_stationRow = FocusedRow();
    if (m_countries.empty() && !m_isLoadingCountries) {
        m_isLoadingCountries = true;
        m_countriesError.clear();
        m_browser->FetchCountries([this](std::vector<RadioCountry> countries, QString error) {
            m_isLoadingCountries = false;
            m_countriesError = error;
            std::sort(countries.begin(), countries.end(), [](const RadioCountry& a, const RadioCountry& b) { return NameLess(a.name, b.name); });
            m_countries = std::move(countries);
            if (m_isPicking) FocusRow(std::max(0, CurrentRow()));
            update();
        });
    }
    FocusRow(std::max(0, CurrentRow()));
}
void RadioPage::CloseCountries(bool isChosen)
{
    m_isPicking = false;
    if (isChosen) FocusRow(m_stationRow);
    else FocusHeader(0);
    if (m_stations.empty() && !m_isLoadingStations) LoadStations();
}
bool RadioPage::Back()
{
    if (!m_isPicking) return false;
    CloseCountries(false);
    return true;
}
QStringList RadioPage::HeaderButtons() const { return {"Country: " + CountryName(m_country)}; }
void RadioPage::PressHeader(int)
{
    if (m_isPicking) CloseCountries(false);
    else OpenCountries();
}
int RadioPage::RowCount() const { return static_cast<int>(m_isPicking ? m_countries.size() : m_stations.size()); }
QString RadioPage::RowText(int row) const
{
    return m_isPicking ? m_countries[static_cast<std::size_t>(row)].name : m_stations[static_cast<std::size_t>(row)].name;
}
QString RadioPage::RowNote(int row) const
{
    if (m_isPicking) return QString::number(m_countries[static_cast<std::size_t>(row)].stationCount);
    const RadioStation& station = m_stations[static_cast<std::size_t>(row)];
    return station.bitrate > 0 ? QString("%1 %2k").arg(station.codec).arg(station.bitrate) : station.codec;
}
int RadioPage::CurrentRow() const
{
    if (!m_isPicking) return m_current;
    for (int i = 0; i < static_cast<int>(m_countries.size()); ++i)
        if (m_countries[static_cast<std::size_t>(i)].code == m_country) return i;
    return -1;
}
void RadioPage::PressRow(int row)
{
    if (!m_isPicking) { PlayStation(row); return; }
    const QString code = m_countries[static_cast<std::size_t>(row)].code;
    if (code != m_country) {
        m_country = code;
        Settings().setValue(kCountrySetting, m_country);
        m_stations.clear();
        m_current = -1;
        m_stationRow = 0;
    }
    CloseCountries(true);
}
QString RadioPage::EmptyText() const
{
    if (m_isPicking) {
        if (m_isLoadingCountries) return "Loading the countries ...";
        if (!m_countriesError.isEmpty()) return "The station directory (radio-browser.info) cannot be reached: " + m_countriesError;
        return "No countries.";
    }
    if (m_isLoadingStations) return "Loading the stations of " + CountryName(m_country) + " ...";
    if (!m_stationsError.isEmpty())
        return "The station directory (radio-browser.info) cannot be reached: " + m_stationsError + "\nOpen and close the country list to try again.";
    return "No stations found for " + CountryName(m_country) + ".";
}
QString RadioPage::NowTitle() const
{
    if (m_current >= 0) return m_stations[static_cast<std::size_t>(m_current)].name;
    return m_stations.empty() ? QString("Radio") : QString("Choose a station");
}
QString RadioPage::NowSubtitle() const
{
    if (IsOwn()) {
        const AudioPlayer::Status& status = LastStatus();
        if (status.state == State::Opening) return "Connecting ...";
        if (status.state == State::Failed) return Text(status.error);
        if (status.state == State::Playing) return status.streamTitle.empty() ? QString("Live") : Text(status.streamTitle);
    }
    if (m_current >= 0) return m_stations[static_cast<std::size_t>(m_current)].tags.split(',').mid(0, 3).join(", ");
    return CountryName(m_country);
}
QString RadioPage::NowInfo() const { return IsOwn() && LastStatus().state == State::Playing ? QString("LIVE") : QString(); }
QString RadioPage::ControlsNote() const
{
    if (m_current < 0) return m_stations.empty() ? QString() : QString("%1 stations").arg(m_stations.size());
    const RadioStation& station = m_stations[static_cast<std::size_t>(m_current)];
    return station.bitrate > 0 ? QString("%1 · %2 kbit/s").arg(station.codec).arg(station.bitrate) : station.codec;
}
menu::Symbol RadioPage::PlaySymbol() const { return IsOwnActive() ? menu::Symbol::Stop : menu::Symbol::Play; }
void RadioPage::PlayStation(int index)
{
    if (index < 0 || index >= static_cast<int>(m_stations.size())) return;
    const RadioStation& station = m_stations[static_cast<std::size_t>(index)];
    m_current = index;
    m_currentId = station.id;
    Settings().setValue(kStationSetting, m_currentId);
    StartPlayer(station.url.toStdString());
    m_browser->CountClick(station.id);
}
void RadioPage::Skip(int direction)
{
    if (!m_isPicking) PlayerPage::Skip(direction);   // browsing the countries does not change the station
}
void RadioPage::Previous()
{
    if (!m_stations.empty()) PlayStation(std::max(0, m_current - 1));
}
void RadioPage::Next()
{
    if (!m_stations.empty()) PlayStation(std::min(static_cast<int>(m_stations.size()) - 1, m_current + 1));
}
void RadioPage::PlayPause()
{
    // A live programme is stopped, not paused: playing again tunes in to what is on now.
    if (IsOwnActive()) Player().Stop();
    else if (m_current >= 0) PlayStation(m_current);
    else if (!m_stations.empty()) PlayStation(m_isPicking ? 0 : FocusedRow());
}
}
