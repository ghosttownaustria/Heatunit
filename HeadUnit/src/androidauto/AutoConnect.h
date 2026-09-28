#pragma once
#include "logging/Logger.h"
#include "usb/RepairResult.h"
#include "usb/UsbProbeResult.h"
#include "usb/UsbTypes.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace headunit {
// The building blocks of "Android Auto verbinden", injected so that the sequencing is tested without a phone;
// ConnectPhoneAutomatically supplies the real ones.
struct AutoConnectDeps {
    std::function<UsbScanResult()> scan;
    // Switch to accessory mode (or restart it) and run one Android Auto session.
    std::function<UsbProbeResult(const UsbDevice&)> connect;
    std::function<RepairResult()> repair;
    // Restart the phone's USB connection (like a cable replug) and repair its driver; one elevated step.
    std::function<RepairResult()> recover;
    std::function<void(std::chrono::milliseconds)> wait;
    // Repair and restart show an administrator prompt (Windows UAC) that the user has to confirm. The step messages
    // only ask for that when it exists.
    bool needsAdminPrompt{};
};

// How a connection attempt ended.
struct AutoConnectResult {
    bool hasVideo{};          // a session with real video ran (and has ended by now)
    bool isStoppedByUser{};
    std::string message;
};

// One user action, all steps: find the phone, repair its USB driver binding when the operating system reverted it
// (Windows), start Android Auto, and when the phone does not answer, restart its USB connection and try again. The run
// ends when a session that showed video has ended, the user stopped it, or every recovery step has been used up.
class AutoConnect {
public:
    AutoConnect(AutoConnectDeps deps, Logger& logger, const std::atomic_bool& isStopRequested, std::function<void(const std::string&)> onStep);

    AutoConnectResult Run();

private:
    AutoConnectDeps m_deps;
    Logger& m_logger;
    const std::atomic_bool& m_isStopRequested;
    std::function<void(const std::string&)> m_onStep;
    std::string m_confirmHint;
    int m_recoveries{};
    int m_retries{};
    int m_repairs{};
    int m_findTries;

    std::optional<AutoConnectResult> FindPhone(std::optional<UsbDevice>& phone);
    std::optional<AutoConnectResult> React(const UsbProbeResult& result);
    std::optional<AutoConnectResult> RepairDriver(const UsbProbeResult& result);
    std::optional<AutoConnectResult> RecoverPhone();
    std::optional<AutoConnectResult> RetryModeSwitch();
    void Step(const std::string& text);
    AutoConnectResult Stopped() const;
    AutoConnectResult Failed(const std::string& message) const;
};

AutoConnectResult RunAutoConnect(const AutoConnectDeps& deps, Logger& logger, const std::atomic_bool& isStopRequested,
    const std::function<void(const std::string&)>& onStep);
void SleepUnlessStopped(std::chrono::milliseconds duration, const std::atomic_bool& isStopRequested);
}
