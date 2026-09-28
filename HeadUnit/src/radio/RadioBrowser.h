#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <vector>

class QNetworkAccessManager;

namespace headunit {
// A country of the station directory.
struct RadioCountry {
    QString code;        // ISO 3166-1, "AT"
    QString name;        // as the directory spells it (English)
    int stationCount{};
};

// A station of the station directory.
struct RadioStation {
    QString id;          // the directory's station UUID
    QString name;
    QString url;         // the stream itself (playlists already resolved by the directory)
    QString codec;       // "MP3", "AAC", ...
    int bitrate{};       // kbit/s, 0 when unknown
    QString tags;
};

// The stations of a country from radio-browser.info, a free community directory of radio stations and their internet
// streams (no key needed). Most stations that broadcast on DAB+ or FM stream the same programme there; receiving the
// broadcast itself needs a tuner (see docs). Requests run asynchronously on the GUI thread's event loop; each callback
// gets the result or an error text. If a directory server does not answer, the next one is tried.
class RadioBrowser final : public QObject {
public:
    explicit RadioBrowser(QObject* parent = nullptr);

    using CountriesDone = std::function<void(std::vector<RadioCountry> countries, QString error)>;
    using StationsDone = std::function<void(std::vector<RadioStation> stations, QString error)>;

    void FetchCountries(CountriesDone done);
    void FetchStations(const QString& countryCode, StationsDone done);
    void CountClick(const QString& stationId);

private:
    QNetworkAccessManager* m_network{};
    unsigned m_stationsRequest{};
    int m_server{};   // the server that answered last

    void Get(const QString& path, std::function<void(QByteArray body, QString error)> done, int attempt = 0);
};
}
