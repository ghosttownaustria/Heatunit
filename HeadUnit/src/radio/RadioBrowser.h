#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

namespace headunit {
struct RadioCountry {
    QString code;        // ISO 3166-1, "AT"
    QString name;        // as the directory spells it (English)
    int stationCount{};
};
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
// broadcast itself needs a tuner (see docs). Requests run asynchronously on the GUI thread's event loop; each
// callback gets the result or an error text. If a directory server does not answer, the next one is tried.
class RadioBrowser final : public QObject {
public:
    explicit RadioBrowser(QObject* parent = nullptr);
    using CountriesDone = std::function<void(std::vector<RadioCountry> countries, QString error)>;
    using StationsDone = std::function<void(std::vector<RadioStation> stations, QString error)>;
    // All countries with at least one station, by name.
    void FetchCountries(CountriesDone done);
    // The working stations of `countryCode`, most listened first. A newer request makes an older one's result moot:
    // only the newest request's callback is called.
    void FetchStations(const QString& countryCode, StationsDone done);
    // Tells the directory a station was played (its popularity ranking relies on it); fire and forget.
    void CountClick(const QString& stationId);
private:
    void Get(const QString& path, std::function<void(QByteArray body, QString error)> done, int attempt = 0);
    QNetworkAccessManager* m_network{};
    unsigned m_stationsRequest{};
    int m_server{};   // the server that answered last
};
}
