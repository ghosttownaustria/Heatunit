#pragma once
#include "media/MusicLibrary.h"
#include "ui/PlayerPage.h"
#include <QString>
#include <vector>

template <typename T>
class QFutureWatcher;

namespace headunit {
// The radio's music player: the music folder (HEADUNIT_MUSIC_DIR, else "HeadUnit" in the user's music folder; created
// when missing) with its sub folders, played in order and on to the next track at the end.
class MultimediaPage final : public PlayerPage {
public:
    MultimediaPage(AudioPlayer& player, QString folder, QWidget* parent = nullptr);
    ~MultimediaPage() override;

    const QString& Folder() const;
    void Rescan();
    void GiveWay() override;

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
    double Progress() const override;
    menu::Symbol PlaySymbol() const override;
    void Previous() override;
    void PlayPause() override;
    void Next() override;
    void OwnStatus(const AudioPlayer::Status& status) override;
    void showEvent(QShowEvent* event) override;

private:
    QString m_folder;
    std::vector<MusicTrack> m_tracks;
    int m_current{-1};
    unsigned m_failuresInARow{};
    QFutureWatcher<std::vector<MusicTrack>>* m_scan{};

    void TakeScan();
    void PlayTrack(int index);
};
}
