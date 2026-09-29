#pragma once
#include "logging/Logger.h"
#include "wireless/BluetoothEvents.h"
#include <QDBusMessage>
#include <QString>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace headunit {
// The one pairing request that waits for the person's answer (BlueZ's call, answered later). The answer functions
// handed out hold it too, and may outlive the service: then they find nothing to answer.
struct PendingPairing {
    std::mutex mutex;
    std::uint64_t id{};
    std::optional<QDBusMessage> request;
    QString devicePath;
};

// What the D-Bus objects of the Bluetooth service share with it: the log and the events for the window, the pairing
// request that waits for its answer, the phones that were connected before the service existed, the phones that
// opened the Android Auto service (with the device each socket belongs to), the phone Android Auto runs on and the phones
// that must be asked for before they connect (Auto Connect off). The D-Bus
// objects run on the service's thread; the phones are handed over to whichever thread waits in WaitForPhone.
class BluetoothContext {
public:
    BluetoothContext(Logger& logger, BluetoothEvents events);

    void Log(LogLevel level, const std::string& message) const;
    void Tell(const std::string& text) const;
    bool CanAskForPairing() const;
    void AskForPairing(const QDBusMessage& message, const QString& devicePath, const std::string& phone, const std::string& code);
    void EndPairing(bool isRefusing);
    void SetManualPhones(std::set<QString> devicePaths);
    bool IsManual(const QString& devicePath);
    void AllowManualConnection(const QString& devicePath);
    void AddPhone(int fd, const QString& devicePath);
    int WaitForPhone(std::chrono::milliseconds timeout);
    void CloseWaitingPhones();
    QString PhonePathOf(int fd);
    void SetAndroidAutoPhone(const QString& devicePath);
    void PublishPhones();
    void PublishNoPhones() const;
    void RememberConnectedBefore(const QString& devicePath);
    void ForgetConnectedBefore(const QString& devicePath);
    bool WasConnectedBefore(const QString& devicePath) const;
    void ClearConnectedBefore();

private:
    Logger& m_logger;
    BluetoothEvents m_events;
    std::shared_ptr<PendingPairing> m_pairing{std::make_shared<PendingPairing>()};
    std::set<QString> m_connectedBefore;   // service thread only
    std::mutex m_mutex;
    std::condition_variable m_hasPhone;
    std::deque<int> m_phones;              // connected RFCOMM sockets of phones that opened the Android Auto service
    std::map<int, QString> m_phonePaths;   // the device of each such socket, also after it was taken
    QString m_androidAutoPhone;            // the device of the running wireless session
    std::set<QString> m_manualPhones;      // phones that connect only when the person asks (Auto Connect off)
    std::map<QString, std::chrono::steady_clock::time_point> m_allowedUntil;   // manual phones the person asked for, until when

    std::function<void(bool)> MakeAnswer(std::uint64_t id) const;
};
}
