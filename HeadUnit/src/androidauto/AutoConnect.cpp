#include "androidauto/AutoConnect.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "usb/UsbLogging.h"
#include <exception>
#include <thread>
#include <utility>

namespace headunit {
namespace {
using namespace std::chrono_literals;
constexpr int kFindTries = 10;                // one scan per second while the phone is missing
constexpr int kFindTriesAfterRecovery = 25;   // a restarted phone needs time to enumerate and load drivers
constexpr int kMaxRecoveries = 2;
constexpr int kMaxPlainRetries = 2;
constexpr int kMaxDriverRepairs = 3;
constexpr int kMaxRounds = 10;

// "04E8:6860 SAMSUNG_Android": how a found phone is named in the steps.
std::string DescribePhone(const UsbDevice& device)
{
    return UsbIdText(device.vendorId, device.productId) + (device.product.empty() ? "" : " " + device.product);
}

// "(1/2)": which try of how many this is.
std::string Count(int number, int maximum)
{
    return "(" + std::to_string(number) + "/" + std::to_string(maximum) + ")";
}
}

// Prepares a run; nothing happens before Run. `onStep` (may be empty) receives every step for the window.
AutoConnect::AutoConnect(AutoConnectDeps deps, Logger& logger, const std::atomic_bool& isStopRequested, std::function<void(const std::string&)> onStep)
    : m_deps(std::move(deps)), m_logger(logger), m_isStopRequested(isStopRequested), m_onStep(std::move(onStep)),
      m_confirmHint(m_deps.needsAdminPrompt ? " - bitte die Windows-Abfrage (Administratorrechte) bestaetigen" : ""), m_findTries(kFindTries)
{
}

// Runs the whole flow, round by round: find the phone, connect it, and react to a failure until nothing is left to try.
AutoConnectResult AutoConnect::Run()
{
    for (int round = 0; round < kMaxRounds; ++round) {
        if (m_isStopRequested) return Stopped();
        Step(round == 0 ? "Suche Android-Handy ..." : "Suche das Handy erneut ...");
        std::optional<UsbDevice> phone;
        if (const auto failure = FindPhone(phone)) return *failure;
        m_findTries = kFindTries;
        const bool isAccessory = DetectAndroidDevice(*phone).evidence == AndroidEvidence::AccessoryMode;
        Step("Handy gefunden: " + DescribePhone(*phone) + (isAccessory ? " (Android-Auto-Modus, wird neu gestartet)" : ""));

        const auto result = m_deps.connect(*phone);
        if (result.state == UsbProbeState::VideoReceived) return {true, false, result.message};
        if (m_isStopRequested) return Stopped();
        if (const auto outcome = React(result)) return *outcome;
    }
    return Failed("Die Verbindung kam nach mehreren Versuchen nicht zustande.");
}

// Scans once a second until exactly one Android device is on the bus. Returns the result that ends the flow when
// there is none, more than one, or the user stopped; otherwise `phone` holds the device.
std::optional<AutoConnectResult> AutoConnect::FindPhone(std::optional<UsbDevice>& phone)
{
    for (int attempt = 0; attempt < m_findTries && !phone && !m_isStopRequested; ++attempt) {
        UsbScanResult scan;
        try {
            scan = m_deps.scan();
        } catch (const std::exception& error) {
            m_logger.Write(LogLevel::Error, "AUTO", error.what());
        }
        int candidates = 0;
        for (const auto& device : scan.devices) {
            if (DetectAndroidDevice(device).evidence == AndroidEvidence::None) continue;
            phone = device;
            ++candidates;
        }
        if (candidates > 1) return Failed("Mehrere Android-Geraete gefunden. Bitte nur ein Handy anschliessen.");
        if (candidates == 0) m_deps.wait(1000ms);
    }
    if (m_isStopRequested) return Stopped();
    if (!phone) return Failed("Kein Android-Handy gefunden. Bitte ein Datenkabel anschliessen und das Handy entsperren.");
    return std::nullopt;
}

// Picks the remedy for a failed attempt. Empty: the next round tries again; otherwise the flow ends with the result.
std::optional<AutoConnectResult> AutoConnect::React(const UsbProbeResult& result)
{
    if (result.canRepairDriver) return RepairDriver(result);
    if (result.needsRecovery && m_recoveries < kMaxRecoveries) return RecoverPhone();
    if (result.isRetryable && m_retries < kMaxPlainRetries) return RetryModeSwitch();
    if (result.isRetryable)
        return Failed("Das Handy wechselt nicht in den Android-Auto-Modus. Bitte das Handy entsperren, Android Auto aktivieren und die Hinweise auf dem Handy bestaetigen, dann erneut verbinden.");
    if (result.needsRecovery)
        return Failed("Android Auto auf dem Handy antwortet auch nach dem Neustart der USB-Verbindung nicht. Bitte das Handy entsperren und pruefen, ob Android Auto aktiviert ist, dann erneut verbinden.");
    return Failed(result.stage + ": " + result.message);
}

// Windows gave the phone Samsung's driver again; WinUSB is needed to talk to it. A repair that never sticks gives up.
std::optional<AutoConnectResult> AutoConnect::RepairDriver(const UsbProbeResult& result)
{
    if (m_repairs >= kMaxDriverRepairs) return Failed("Der USB-Treiber des Handys laesst sich nicht dauerhaft reparieren: " + result.message);
    ++m_repairs;
    Step("Windows hat dem Handy den falschen USB-Treiber zugewiesen. Repariere ihn" + m_confirmHint + " ...");
    const auto repaired = m_deps.repair();
    if (!repaired.IsUsable()) return Failed(repaired.message);
    Step(repaired.message);
    m_deps.wait(2000ms);
    return std::nullopt;
}

// Accessory mode is up but Android Auto on the phone stays silent, or the phone's USB link does not answer at all.
// Restarting the phone's USB connection (the same as unplugging and replugging the cable) fixes both.
std::optional<AutoConnectResult> AutoConnect::RecoverPhone()
{
    ++m_recoveries;
    Step("Das Handy reagiert nicht. Starte die USB-Verbindung des Handys neu " + Count(m_recoveries, kMaxRecoveries) + m_confirmHint + " ...");
    const auto recovered = m_deps.recover();
    if (!recovered.IsUsable()) return Failed(recovered.message + " Alternativ das Kabel einmal abziehen und wieder anstecken.");
    Step(recovered.message);
    m_deps.wait(2000ms);
    m_findTries = kFindTriesAfterRecovery;
    return std::nullopt;
}

// The phone did not switch to accessory mode. That is usually a locked screen or a prompt that has not been confirmed
// yet; restarting USB would not change either, so this only waits and asks again.
std::optional<AutoConnectResult> AutoConnect::RetryModeSwitch()
{
    ++m_retries;
    Step("Das Handy ist nicht in den Android-Auto-Modus gewechselt. Bitte das Handy entsperren und Hinweise auf dem Handy bestaetigen. Neuer Versuch " +
        Count(m_retries, kMaxPlainRetries) + " ...");
    m_deps.wait(3000ms);
    return std::nullopt;
}

// Logs a step and hands it to the window.
void AutoConnect::Step(const std::string& text)
{
    m_logger.Write(LogLevel::Info, "AUTO", text);
    if (m_onStep) m_onStep(text);
}

// The result of a run the user ended.
AutoConnectResult AutoConnect::Stopped() const
{
    return {false, true, "Verbindung beendet."};
}

// The result of a run that gave up with `message` (logged as the error).
AutoConnectResult AutoConnect::Failed(const std::string& message) const
{
    m_logger.Write(LogLevel::Error, "AUTO", message);
    return {false, false, message};
}

// Runs the flow once (see AutoConnect).
AutoConnectResult RunAutoConnect(const AutoConnectDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    const std::function<void(const std::string&)>& onStep)
{
    return AutoConnect(deps, logger, isStopRequested, onStep).Run();
}

// Waits `duration` in slices of 100 ms and returns early once `isStopRequested` is set, so a stop takes effect at once.
void SleepUnlessStopped(std::chrono::milliseconds duration, const std::atomic_bool& isStopRequested)
{
    for (auto left = duration; left.count() > 0 && !isStopRequested; left -= 100ms) std::this_thread::sleep_for(100ms);
}
}
