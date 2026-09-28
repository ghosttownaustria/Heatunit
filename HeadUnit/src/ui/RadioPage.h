#pragma once
#include "radio/RadioBrowser.h"
#include "ui/PlayerPage.h"
#include <QString>
#include <vector>

namespace headunit {
// The radio's tuner: the stations of one country from the radio-browser.info directory (the most listened ones, listed
// alphabetically), played as internet streams. The country button at the top opens the list of countries; the choice
// and the last station are remembered.
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
    void Skip(int direction) override;
    void showEvent(QShowEvent* event) override;

private:
    RadioBrowser* m_browser{};
    QString m_country;                     // ISO code
    std::vector<RadioCountry> m_countries;
    std::vector<RadioStation> m_stations;
    bool m_isPicking{};                    // the list shows the countries
    bool m_isLoadingStations{};
    bool m_isLoadingCountries{};
    QString m_stationsError;
    QString m_countriesError;
    int m_current{-1};                     // station that plays or played last
    QString m_currentId;
    int m_stationRow{};                    // where the focus was in the stations while the countries are open

    void LoadStations();
    void TakeStations(std::vector<RadioStation> stations, const QString& error);
    void OpenCountries();
    void TakeCountries(std::vector<RadioCountry> countries, const QString& error);
    void CloseCountries(bool isChosen);
    void ChooseCountry(int row);
    void PlayStation(int index);
};
}
