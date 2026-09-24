#pragma once
#include "media/AudioPlayer.h"
#include "media/MusicLibrary.h"
#include "radio/RadioBrowser.h"
#include "ui/HomeMenuLayout.h"
#include "ui/MenuPage.h"
#include "ui/MenuStyle.h"
#include <QStringList>
#include <functional>
#include <optional>
#include <vector>

template <typename T> class QFutureWatcher;

namespace headunit {
// A page of the radio that plays something (the music player, the tuner). At the left its tile from the home menu, lit
// while the page's source plays; at the right what plays now, the player's controls (previous, play, next) and a list,
// with buttons at the top right. The controller moves a focus through these (PageFocus in HomeMenuLayout.h) and
// pushing activates what it is on; the mouse clicks them directly. Both pages share the one AudioPlayer: whichever
// started it last owns it, and only the owner reacts to its end or to media keys.
class PlayerPage : public MenuPage {
public:
    PlayerPage(HomeMenuEntry entry, AudioPlayer& player, QWidget* parent);
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;
    // A media key (keys::MediaPlayPause, MediaNext, ...) while this page's source plays or is paused: handled here and
    // true. False when the player is not this page's (the key then belongs to the phone).
    bool MediaKey(unsigned keycode);
    // Called before this page starts the player (the window pauses the phone's music then).
    std::function<void()> onWillPlay;
protected:
    // What the page shows and does.
    virtual QStringList HeaderButtons() const = 0;
    virtual void PressHeader(int index) = 0;
    virtual int RowCount() const = 0;
    virtual QString RowText(int row) const = 0;
    virtual QString RowNote(int row) const = 0;
    virtual int CurrentRow() const = 0;           // what plays or played last, orange in the list; -1 for none
    virtual void PressRow(int row) = 0;
    virtual QString EmptyText() const = 0;        // in place of an empty list
    virtual QString NowTitle() const = 0;
    virtual QString NowSubtitle() const = 0;
    virtual QString NowInfo() const = 0;          // at the right of the subtitle: "1:23 / 3:45", "LIVE"
    virtual QString ControlsNote() const = 0;     // dim, at the right of the controls
    virtual double Progress() const { return -1; }   // 0..1; negative for no progress bar
    virtual menu::Symbol PlaySymbol() const = 0;
    virtual void Previous() = 0;
    virtual void PlayPause() = 0;
    virtual void Next() = 0;
    // Called about four times a second while the player is this page's (to go on after the end of a track).
    virtual void OwnStatus(const AudioPlayer::Status&) {}

    AudioPlayer& Player() { return m_player; }
    // The player's state as of the last look; `IsOwn` says whether it is this page's source.
    const AudioPlayer::Status& LastStatus() const { return m_status; }
    bool IsOwn() const { return m_generation != 0 && m_status.generation == m_generation; }
    bool IsOwnActive() const;
    bool IsOwnSounding() const { return IsOwn() && (m_status.state == AudioPlayer::State::Opening || m_status.state == AudioPlayer::State::Playing); }
    void StartPlayer(const std::string& source);
    // Puts the controller on a row of the list; a row out of view is brought to the middle of it.
    void FocusRow(int row);
    // Puts the controller on a button at the top.
    void FocusHeader(int index);
    // After the list changed: the focus and the scrolling made valid again.
    void ListChanged();
    int FocusedRow() const { return m_focus.row; }

    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
private:
    struct Hit { PageFocus::Part part; int index; bool isTile; friend bool operator==(const Hit&, const Hit&) = default; };
    PageShape Shape() const;
    void Activate(PageFocus::Part part, int index);
    void Refocus(PageFocus focus);
    void Look();
    double Right() const;
    QRectF HeaderRect(int index) const;
    QRectF ControlRect(int index) const;
    QRectF RowRect(int visibleIndex) const;
    std::optional<Hit> HitAt(const QPointF& position) const;
    HomeMenuEntry m_entry;
    AudioPlayer& m_player;
    AudioPlayer::Status m_status;
    unsigned m_generation{};   // of this page's last Play
    PageFocus m_focus;
    int m_firstRow{};
    std::optional<Hit> m_pressed;
};

// The radio's music player: the music folder (HEADUNIT_MUSIC_DIR, else "HeadUnit" in the user's music folder; created
// when missing) with its sub folders, played in order and on to the next track at the end.
class MultimediaPage final : public PlayerPage {
public:
    MultimediaPage(AudioPlayer& player, QString folder, QWidget* parent = nullptr);
    ~MultimediaPage() override;
    const QString& Folder() const { return m_folder; }
    // Reads the folder again (in the background); the playing track keeps playing.
    void Rescan();
protected:
    QStringList HeaderButtons() const override { return {"Open folder", "Rescan"}; }
    void PressHeader(int index) override;
    int RowCount() const override { return static_cast<int>(m_tracks.size()); }
    QString RowText(int row) const override;
    QString RowNote(int row) const override;
    int CurrentRow() const override { return m_current; }
    void PressRow(int row) override { PlayTrack(row); }
    QString EmptyText() const override;
    QString NowTitle() const override;
    QString NowSubtitle() const override;
    QString NowInfo() const override;
    QString ControlsNote() const override;
    double Progress() const override;
    menu::Symbol PlaySymbol() const override;
    void Previous() override;
    void PlayPause() override;
    void Next() override;
    void OwnStatus(const AudioPlayer::Status& status) override;
    void showEvent(QShowEvent* event) override;
private:
    void PlayTrack(int index);
    QString m_folder;
    std::vector<MusicTrack> m_tracks;
    int m_current{-1};
    unsigned m_failuresInARow{};
    QFutureWatcher<std::vector<MusicTrack>>* m_scan{};
};

// The radio's tuner: the stations of one country from the radio-browser.info directory (the most listened ones, listed
// alphabetically), played as internet streams.
// The country button at the top opens the list of countries; the choice and the last station are remembered.
class RadioPage final : public PlayerPage {
public:
    explicit RadioPage(AudioPlayer& player, QWidget* parent = nullptr);
    bool Back() override;
protected:
    QStringList HeaderButtons() const override;
    void PressHeader(int index) override;
    int RowCount() const override;
    QString RowText(int row) const override;
    QString RowNote(int row) const override;
    int CurrentRow() const override;
    void PressRow(int row) override;
    QString EmptyText() const override;
    QString NowTitle() const override;
    QString NowSubtitle() const override;
    QString NowInfo() const override;
    QString ControlsNote() const override;
    menu::Symbol PlaySymbol() const override;
    void Previous() override;
    void PlayPause() override;
    void Next() override;
    void showEvent(QShowEvent* event) override;
private:
    void LoadStations();
    void OpenCountries();
    // `isChosen`: a country was picked, the focus goes to its stations; otherwise back to the country button.
    void CloseCountries(bool isChosen);
    void PlayStation(int index);
    RadioBrowser* m_browser{};
    QString m_country;                     // ISO code
    std::vector<RadioCountry> m_countries;
    std::vector<RadioStation> m_stations;
    bool m_isPicking{};                    // the list shows the countries
    bool m_isLoadingStations{}, m_isLoadingCountries{};
    QString m_stationsError, m_countriesError;
    int m_current{-1};                     // station that plays or played last
    QString m_currentId;
    int m_stationRow{};                    // where the focus was in the stations while the countries are open
};
}
