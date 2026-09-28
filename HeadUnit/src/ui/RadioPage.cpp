#include "ui/RadioPage.h"
#include <QCollator>
#include <QLocale>
#include <QSettings>
#include <algorithm>
#include <utility>

namespace headunit {
namespace {
using State = AudioPlayer::State;
// Remembered between runs, like the display size.
constexpr const char* kCountrySetting = "radio/country";
constexpr const char* kStationSetting = "radio/station";

// The country's name in the user's language; its code when Qt does not know it.
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
bool NameLess(const QString& first, const QString& second)
{
    static const QCollator collator = [] {
        QCollator result;
        result.setCaseSensitivity(Qt::CaseInsensitive);
        result.setNumericMode(true);
        return result;
    }();
    return collator.compare(first, second) < 0;
}
}

// A tuner for the remembered country (else the system's) and the remembered station; the directory is asked only once
// the page is shown.
RadioPage::RadioPage(AudioPlayer& player, QWidget* parent) : PlayerPage(HomeMenuEntry::Radio, player, parent), m_browser(new RadioBrowser(this))
{
    const QSettings settings;
    m_country = settings.value(kCountrySetting).toString().toUpper();
    if (m_country.size() != 2) m_country = SystemCountry();
    m_currentId = settings.value(kStationSetting).toString();
}

// Back closes the list of countries.
bool RadioPage::Back()
{
    if (!m_isPicking) return false;
    CloseCountries(false);
    return true;
}

// The country button.
QStringList RadioPage::HeaderButtons() const
{
    return {"Country: " + CountryName(m_country)};
}

// The country button opens or closes the list of countries.
void RadioPage::PressHeader(int)
{
    if (m_isPicking) CloseCountries(false);
    else OpenCountries();
}

// The countries or the stations.
int RadioPage::RowCount() const
{
    return static_cast<int>(m_isPicking ? m_countries.size() : m_stations.size());
}

// A country's or a station's name.
QString RadioPage::RowText(int row) const
{
    return m_isPicking ? m_countries[static_cast<std::size_t>(row)].name : m_stations[static_cast<std::size_t>(row)].name;
}

// A country's number of stations, or a station's codec and bitrate.
QString RadioPage::RowNote(int row) const
{
    if (m_isPicking) return QString::number(m_countries[static_cast<std::size_t>(row)].stationCount);
    const RadioStation& station = m_stations[static_cast<std::size_t>(row)];
    return station.bitrate > 0 ? QString("%1 %2k").arg(station.codec).arg(station.bitrate) : station.codec;
}

// The chosen country, or the station that plays or played last.
int RadioPage::CurrentRow() const
{
    if (!m_isPicking) return m_current;
    for (int index = 0; index < static_cast<int>(m_countries.size()); ++index) {
        if (m_countries[static_cast<std::size_t>(index)].code == m_country) return index;
    }
    return -1;
}

// A station plays; a country is chosen.
void RadioPage::PressRow(int row)
{
    if (m_isPicking) ChooseCountry(row);
    else PlayStation(row);
}

// While loading, why the directory cannot be reached, or that there is nothing.
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

// The station's name.
QString RadioPage::NowTitle() const
{
    if (m_current >= 0) return m_stations[static_cast<std::size_t>(m_current)].name;
    return m_stations.empty() ? QString("Radio") : QString("Choose a station");
}

// Connecting, a failure, what the station says is playing, its tags, or the country.
QString RadioPage::NowSubtitle() const
{
    if (IsOwn()) {
        const AudioPlayer::Status& status = LastStatus();
        if (status.state == State::Opening) return "Connecting ...";
        if (status.state == State::Failed) return QString::fromStdString(status.error);
        if (status.state == State::Playing) return status.streamTitle.empty() ? QString("Live") : QString::fromStdString(status.streamTitle);
    }
    if (m_current >= 0) return m_stations[static_cast<std::size_t>(m_current)].tags.split(',').mid(0, 3).join(", ");
    return CountryName(m_country);
}

// "LIVE" while a station plays.
QString RadioPage::NowInfo() const
{
    return IsOwn() && LastStatus().state == State::Playing ? QString("LIVE") : QString();
}

// The station's codec and bitrate, or how many stations there are.
QString RadioPage::ControlsNote() const
{
    if (m_current < 0) return m_stations.empty() ? QString() : QString("%1 stations").arg(m_stations.size());
    const RadioStation& station = m_stations[static_cast<std::size_t>(m_current)];
    return station.bitrate > 0 ? QString::fromUtf8("%1 · %2 kbit/s").arg(station.codec).arg(station.bitrate) : station.codec;
}

// Stop while a station plays, play otherwise.
menu::Symbol RadioPage::PlaySymbol() const
{
    return IsOwnActive() ? menu::Symbol::Stop : menu::Symbol::Play;
}

// The station before.
void RadioPage::Previous()
{
    if (!m_stations.empty()) PlayStation(std::max(0, m_current - 1));
}

// A live programme is stopped, not paused: playing again tunes in to what is on now.
void RadioPage::PlayPause()
{
    if (IsOwnActive()) Player().Stop();
    else if (m_current >= 0) PlayStation(m_current);
    else if (!m_stations.empty()) PlayStation(m_isPicking ? 0 : FocusedRow());
}

// The station after.
void RadioPage::Next()
{
    if (!m_stations.empty()) PlayStation(std::min(static_cast<int>(m_stations.size()) - 1, m_current + 1));
}

// Browsing the countries does not change the station.
void RadioPage::Skip(int direction)
{
    if (!m_isPicking) PlayerPage::Skip(direction);
}

// The directory is asked only once the tuner is opened, and again when it failed before.
void RadioPage::showEvent(QShowEvent* event)
{
    PlayerPage::showEvent(event);
    if (m_stations.empty() && !m_isLoadingStations) LoadStations();
}

// Asks the directory for the country's stations.
void RadioPage::LoadStations()
{
    m_isLoadingStations = true;
    m_stationsError.clear();
    update();
    m_browser->FetchStations(m_country, [this](std::vector<RadioStation> stations, QString error) { TakeStations(std::move(stations), error); });
}

// The directory sends the most listened stations; the list shows them alphabetically (stations of the same name keep
// their popularity order). The remembered station is the current one again.
void RadioPage::TakeStations(std::vector<RadioStation> stations, const QString& error)
{
    m_isLoadingStations = false;
    m_stationsError = error;
    std::stable_sort(stations.begin(), stations.end(), [](const RadioStation& first, const RadioStation& second) { return NameLess(first.name, second.name); });
    m_stations = std::move(stations);
    m_current = -1;
    for (int index = 0; index < static_cast<int>(m_stations.size()); ++index) {
        if (m_stations[static_cast<std::size_t>(index)].id == m_currentId) m_current = index;
    }
    m_stationRow = std::max(0, m_current);
    if (!m_isPicking) FocusRow(m_stationRow);
    update();
}

// Shows the list of countries (loaded once), with the focus on the chosen one.
void RadioPage::OpenCountries()
{
    m_isPicking = true;
    m_stationRow = FocusedRow();
    if (m_countries.empty() && !m_isLoadingCountries) {
        m_isLoadingCountries = true;
        m_countriesError.clear();
        m_browser->FetchCountries([this](std::vector<RadioCountry> countries, QString error) { TakeCountries(std::move(countries), error); });
    }
    FocusRow(std::max(0, CurrentRow()));
}

// The countries arrived: listed by name.
void RadioPage::TakeCountries(std::vector<RadioCountry> countries, const QString& error)
{
    m_isLoadingCountries = false;
    m_countriesError = error;
    std::sort(countries.begin(), countries.end(), [](const RadioCountry& first, const RadioCountry& second) { return NameLess(first.name, second.name); });
    m_countries = std::move(countries);
    if (m_isPicking) FocusRow(std::max(0, CurrentRow()));
    update();
}

// Back to the stations. `isChosen`: a country was picked, the focus goes to its stations; otherwise back to the country
// button. Stations that are missing (a new country, or a failed load) are asked for.
void RadioPage::CloseCountries(bool isChosen)
{
    m_isPicking = false;
    if (isChosen) FocusRow(m_stationRow);
    else FocusHeader(0);
    if (m_stations.empty() && !m_isLoadingStations) LoadStations();
}

// The country of `row` becomes the chosen one (remembered); a new country starts with no stations.
void RadioPage::ChooseCountry(int row)
{
    const QString code = m_countries[static_cast<std::size_t>(row)].code;
    if (code != m_country) {
        m_country = code;
        QSettings().setValue(kCountrySetting, m_country);
        m_stations.clear();
        m_current = -1;
        m_stationRow = 0;
    }
    CloseCountries(true);
}

// Plays station `index` (remembered), and tells the directory it was played (its popularity ranking relies on it).
void RadioPage::PlayStation(int index)
{
    if (index < 0 || index >= static_cast<int>(m_stations.size())) return;
    const RadioStation& station = m_stations[static_cast<std::size_t>(index)];
    m_current = index;
    m_currentId = station.id;
    QSettings().setValue(kStationSetting, m_currentId);
    StartPlayer(station.url.toStdString());
    m_browser->CountClick(station.id);
}
}
