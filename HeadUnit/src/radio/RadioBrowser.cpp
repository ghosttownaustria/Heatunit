#include "radio/RadioBrowser.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <algorithm>
#include <iterator>
#include <utility>

namespace headunit {
namespace {
// The directory's servers; any of them has all stations.
const char* const kServers[] = {"https://de1.api.radio-browser.info", "https://de2.api.radio-browser.info", "https://all.api.radio-browser.info"};
constexpr int kServerCount = static_cast<int>(std::size(kServers));
constexpr int kTimeoutMs = 8000;
constexpr int kStationLimit = 500;
}

RadioBrowser::RadioBrowser(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void RadioBrowser::Get(const QString& path, std::function<void(QByteArray, QString)> done, int attempt)
{
    const int server = (m_server + attempt) % kServerCount;
    QNetworkRequest request(QUrl(QString::fromLatin1(kServers[server]) + path));
    // The directory asks clients to name themselves.
    request.setHeader(QNetworkRequest::UserAgentHeader, "HeadUnit/0.1");
    request.setTransferTimeout(kTimeoutMs);
    QNetworkReply* reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, done = std::move(done), attempt, server] {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_server = server;
            done(reply->readAll(), {});
        } else if (attempt + 1 < kServerCount) {
            Get(path, done, attempt + 1);
        } else {
            done({}, reply->errorString());
        }
    });
}
void RadioBrowser::FetchCountries(CountriesDone done)
{
    Get("/json/countries", [done = std::move(done)](QByteArray body, QString error) {
        std::vector<RadioCountry> countries;
        if (error.isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            if (!document.isArray()) error = "Antwort des Senderverzeichnisses ist unlesbar";
            for (const QJsonValue value : document.array()) {
                const QJsonObject object = value.toObject();
                RadioCountry country{object.value("iso_3166_1").toString().toUpper(), object.value("name").toString(), object.value("stationcount").toInt()};
                if (country.code.size() == 2 && country.stationCount > 0) countries.push_back(std::move(country));
            }
        }
        done(std::move(countries), error);
    });
}
void RadioBrowser::FetchStations(const QString& countryCode, StationsDone done)
{
    const unsigned request = ++m_stationsRequest;
    const QString path = "/json/stations/bycountrycodeexact/" + QUrl::toPercentEncoding(countryCode) +
        QString("?hidebroken=true&order=clickcount&reverse=true&limit=%1").arg(kStationLimit);
    Get(path, [this, request, done = std::move(done)](QByteArray body, QString error) {
        if (request != m_stationsRequest) return;   // the user has chosen another country meanwhile
        std::vector<RadioStation> stations;
        if (error.isEmpty()) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            if (!document.isArray()) error = "Antwort des Senderverzeichnisses ist unlesbar";
            for (const QJsonValue value : document.array()) {
                const QJsonObject object = value.toObject();
                RadioStation station;
                station.id = object.value("stationuuid").toString();
                station.name = object.value("name").toString().simplified();
                station.url = object.value("url_resolved").toString();
                if (station.url.isEmpty()) station.url = object.value("url").toString();
                station.codec = object.value("codec").toString();
                station.bitrate = object.value("bitrate").toInt();
                station.tags = object.value("tags").toString();
                const bool isStream = station.url.startsWith("http://") || station.url.startsWith("https://");
                if (!station.name.isEmpty() && isStream) stations.push_back(std::move(station));
            }
        }
        done(std::move(stations), error);
    });
}
void RadioBrowser::CountClick(const QString& stationId)
{
    if (stationId.isEmpty()) return;
    Get("/json/url/" + QUrl::toPercentEncoding(stationId), [](QByteArray, QString) {});
}
}
