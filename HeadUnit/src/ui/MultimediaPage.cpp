#include "ui/MultimediaPage.h"
#include "media/StreamText.h"
#include <QDesktopServices>
#include <QDir>
#include <QFutureWatcher>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

namespace headunit {
namespace {
using State = AudioPlayer::State;
// Like a car stereo: this many seconds into a title, Previous means its start.
constexpr double kRestartSeconds = 3;
}

// A player of `folder` (created when missing), which is read in the background.
MultimediaPage::MultimediaPage(AudioPlayer& player, QString folder, QWidget* parent)
    : PlayerPage(HomeMenuEntry::Multimedia, player, parent), m_folder(std::move(folder)), m_scan(new QFutureWatcher<std::vector<MusicTrack>>(this))
{
    QDir().mkpath(m_folder);
    connect(m_scan, &QFutureWatcher<std::vector<MusicTrack>>::finished, this, &MultimediaPage::TakeScan);
    Rescan();
}

// A scan still running must finish before the page goes.
MultimediaPage::~MultimediaPage()
{
    m_scan->waitForFinished();
}

// The music folder.
const QString& MultimediaPage::Folder() const
{
    return m_folder;
}

// Reads the folder again (in the background); the playing track keeps playing.
void MultimediaPage::Rescan()
{
    if (m_scan->isRunning()) return;
    const QByteArray utf8 = m_folder.toUtf8();
    const std::filesystem::path folder(std::u8string(utf8.begin(), utf8.end()));
    m_scan->setFuture(QtConcurrent::run([folder] { return ScanMusicFolder(folder); }));
    update();
}

// The phone's music started: the music pauses, so that the title goes on where it was.
void MultimediaPage::GiveWay()
{
    if (IsOwnSoundingNow()) Player().SetPaused(true);
}

// Opening the folder in the file manager, and reading it again.
QStringList MultimediaPage::HeaderButtons() const
{
    return {"Open folder", "Rescan"};
}

// Opens the folder or reads it again.
void MultimediaPage::PressHeader(int index)
{
    if (index == 0) QDesktopServices::openUrl(QUrl::fromLocalFile(m_folder));
    else Rescan();
}

// One row per track.
int MultimediaPage::RowCount() const
{
    return static_cast<int>(m_tracks.size());
}

// The track's name.
QString MultimediaPage::RowText(int row) const
{
    return QString::fromStdString(m_tracks[static_cast<std::size_t>(row)].name);
}

// The track's sub folder.
QString MultimediaPage::RowNote(int row) const
{
    return QString::fromStdString(m_tracks[static_cast<std::size_t>(row)].folder);
}

// The track that plays or played last.
int MultimediaPage::CurrentRow() const
{
    return m_current;
}

// A track was chosen: it plays.
void MultimediaPage::PressRow(int row)
{
    PlayTrack(row);
}

// While reading, or how to fill an empty folder.
QString MultimediaPage::EmptyText() const
{
    if (m_scan->isRunning()) return "Reading the music folder ...";
    return "No music yet. Copy music files (MP3, FLAC, M4A, OGG, OPUS, WAV) into\n" + QDir::toNativeSeparators(m_folder) +
        "\nthen choose Rescan. Sub folders (albums) are fine.";
}

// The file's title tag, else the track's name.
QString MultimediaPage::NowTitle() const
{
    if (IsOwn() && !LastStatus().title.empty()) return QString::fromStdString(LastStatus().title);
    if (m_current >= 0) return RowText(m_current);
    return m_tracks.empty() ? QString("Music folder") : QString("Choose a title");
}

// A failure, the opening, the artist tag or the track's sub folder.
QString MultimediaPage::NowSubtitle() const
{
    if (IsOwn()) {
        const AudioPlayer::Status& status = LastStatus();
        if (status.state == State::Failed) return QString::fromStdString(status.error);
        if (status.state == State::Opening) return "Opening ...";
        if (!status.artist.empty()) return QString::fromStdString(status.artist);
    }
    if (m_current >= 0 && !m_tracks[static_cast<std::size_t>(m_current)].folder.empty()) return RowNote(m_current);
    return m_tracks.empty() ? QDir::toNativeSeparators(m_folder) : QString("Music folder");
}

// "1:23 / 3:45" (with "Paused" before it while paused).
QString MultimediaPage::NowInfo() const
{
    if (!IsOwn() || LastStatus().duration <= 0) return {};
    const AudioPlayer::Status& status = LastStatus();
    return (status.state == State::Paused ? QString::fromUtf8("Paused · ") : QString()) + QString::fromStdString(FormatPlayTime(status.position)) +
        " / " + QString::fromStdString(FormatPlayTime(status.duration));
}

// How many titles the folder holds.
QString MultimediaPage::ControlsNote() const
{
    return m_tracks.size() == 1 ? QString("1 title") : QString("%1 titles").arg(m_tracks.size());
}

// How far the title has played.
double MultimediaPage::Progress() const
{
    if (!IsOwn() || LastStatus().duration <= 0) return -1;
    return LastStatus().position / LastStatus().duration;
}

// Pause while it plays, play otherwise.
menu::Symbol MultimediaPage::PlaySymbol() const
{
    return IsOwnSounding() ? menu::Symbol::Pause : menu::Symbol::Play;
}

// Like a car stereo: a few seconds into a title, back means its start; otherwise the title before.
void MultimediaPage::Previous()
{
    if (m_tracks.empty()) return;
    if (IsOwnActive() && m_current >= 0 && Player().CurrentStatus().position > kRestartSeconds) PlayTrack(m_current);
    else PlayTrack(std::max(0, m_current - 1));
}

// Pauses or resumes the title; without one plays the focused title (resuming takes the sound back from the phone).
void MultimediaPage::PlayPause()
{
    if (IsOwnActive()) {
        const bool isPaused = Player().CurrentStatus().state == State::Paused;
        if (isPaused) NotifyWillPlay();
        Player().SetPaused(!isPaused);
    } else if (!m_tracks.empty()) {
        PlayTrack(m_current >= 0 ? m_current : FocusedRow());
    }
}

// The next title, if there is one.
void MultimediaPage::Next()
{
    if (m_current + 1 < RowCount()) PlayTrack(m_current + 1);
}

// On to the next title at the end of one; a file that cannot be played is skipped, but not endlessly.
void MultimediaPage::OwnStatus(const AudioPlayer::Status& status)
{
    if (status.state == State::Playing) {
        m_failuresInARow = 0;
    } else if (status.state == State::Ended) {
        m_failuresInARow = 0;
        Next();
    } else if (status.state == State::Failed && ++m_failuresInARow < m_tracks.size()) {
        Next();
    }
}

// Files may have been added since the page was last shown.
void MultimediaPage::showEvent(QShowEvent* event)
{
    PlayerPage::showEvent(event);
    Rescan();
}

// Takes the finished scan. The playing track stays the current one wherever it lands in the new list.
void MultimediaPage::TakeScan()
{
    const std::string playing = m_current >= 0 && m_current < RowCount() ? m_tracks[static_cast<std::size_t>(m_current)].path : std::string();
    m_tracks = m_scan->result();
    m_current = -1;
    for (int index = 0; index < RowCount(); ++index) {
        if (m_tracks[static_cast<std::size_t>(index)].path == playing) m_current = index;
    }
    ListChanged();
    update();
}

// Plays track `index`.
void MultimediaPage::PlayTrack(int index)
{
    if (index < 0 || index >= RowCount()) return;
    m_current = index;
    StartPlayer(m_tracks[static_cast<std::size_t>(index)].path);
}
}
