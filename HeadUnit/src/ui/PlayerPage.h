#pragma once
#include "media/AudioPlayer.h"
#include "ui/HomeMenuEntry.h"
#include "ui/MenuPage.h"
#include "ui/MenuStyle.h"
#include "ui/PageFocus.h"
#include <QStringList>
#include <functional>
#include <optional>
#include <string>

namespace headunit {
// A page of the radio that plays something (the music player, the tuner). At the left its tile from the home menu, lit
// while the page's source plays; at the right what plays now, the player's controls (previous, play, next) and a list,
// with buttons at the top right. The controller moves a focus through these (PageFocus.h): turning moves within a part,
// up and down jump between the parts, pushing activates what the focus is on; left and right skip to the previous or
// next title (station). The mouse clicks them directly. Both pages share the one AudioPlayer: whichever started it last
// owns it, and only the owner reacts to its end, to media keys and to the phone taking over.
class PlayerPage : public MenuPage {
public:
    PlayerPage(HomeMenuEntry entry, AudioPlayer& player, QWidget* parent);

    void SetWillPlayHandler(std::function<void()> handler);
    void Turn(int steps) override;
    void Nudge(unsigned keycode) override;
    void Push() override;
    bool MediaKey(unsigned keycode);
    virtual void GiveWay();

protected:
    // The buttons at the top right.
    virtual QStringList HeaderButtons() const = 0;
    // A button at the top was activated.
    virtual void PressHeader(int index) = 0;
    // The number of rows of the list.
    virtual int RowCount() const = 0;
    // The text of a row.
    virtual QString RowText(int row) const = 0;
    // The dim note at the right of a row.
    virtual QString RowNote(int row) const = 0;
    // What plays or played last, orange in the list; -1 for none.
    virtual int CurrentRow() const = 0;
    // A row was activated.
    virtual void PressRow(int row) = 0;
    // What shows in place of an empty list.
    virtual QString EmptyText() const = 0;
    // The large line of what plays now.
    virtual QString NowTitle() const = 0;
    // The line below it.
    virtual QString NowSubtitle() const = 0;
    // At the right of the subtitle: "1:23 / 3:45", "LIVE".
    virtual QString NowInfo() const = 0;
    // Dim, at the right of the controls.
    virtual QString ControlsNote() const = 0;
    // The symbol of the middle control.
    virtual menu::Symbol PlaySymbol() const = 0;
    // The previous title (station).
    virtual void Previous() = 0;
    // Plays, pauses or stops, as the page's source needs it.
    virtual void PlayPause() = 0;
    // The next title (station).
    virtual void Next() = 0;
    virtual double Progress() const;
    virtual void Skip(int direction);
    virtual void OwnStatus(const AudioPlayer::Status& status);
    AudioPlayer& Player();
    const AudioPlayer::Status& LastStatus() const;
    bool IsOwn() const;
    bool IsOwnActive() const;
    bool IsOwnSoundingNow() const;
    bool IsOwnSounding() const;
    void StartPlayer(const std::string& source);
    void NotifyWillPlay();
    void FocusRow(int row);
    void FocusHeader(int index);
    void ListChanged();
    int FocusedRow() const;
    void Paint(QPainter& painter) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // What a click hits: a part of the page and the index in it, or the tile.
    struct Hit {
        friend bool operator==(const Hit&, const Hit&) = default;

        PageFocus::Part part;
        int index;
        bool isTile;
    };

    std::function<void()> m_onWillPlay;
    HomeMenuEntry m_entry;
    AudioPlayer& m_player;
    AudioPlayer::Status m_status;
    unsigned m_generation{};   // of this page's last Play
    PageFocus m_focus;
    int m_firstRow{};
    std::optional<Hit> m_pressed;

    PageShape Shape() const;
    void Activate(PageFocus::Part part, int index);
    void Refocus(PageFocus focus);
    void Look();
    double Right() const;
    QRectF HeaderRect(int index) const;
    QRectF ControlRect(int index) const;
    QRectF RowRect(int visibleIndex) const;
    std::optional<Hit> HitAt(const QPointF& position) const;
    void PaintNowPlaying(QPainter& painter) const;
    void PaintControls(QPainter& painter) const;
    void PaintList(QPainter& painter) const;
};
}
