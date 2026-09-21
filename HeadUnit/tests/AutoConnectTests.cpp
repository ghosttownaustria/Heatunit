#include "androidauto/AutoConnect.h"
#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <vector>

using namespace headunit;
void Check(bool isValid, const char* message);

namespace {
UsbDevice Phone(std::uint16_t vendor = 0x04e8, std::uint16_t product = 0x6860) {
    UsbDevice device;
    device.vendorId = vendor;
    device.productId = product;
    device.product = "SAMSUNG_Android";
    device.location = "hub/port-2";
    return device;
}
UsbProbeResult Video() { return {UsbProbeState::VideoReceived, "Android Auto session", "Android Auto stopped by user"}; }
UsbProbeResult NoAnswer() {
    UsbProbeResult result{UsbProbeState::Failed, "Android Auto session", "The phone did not answer the version request."};
    result.needsRecovery = true;
    return result;
}
UsbProbeResult NoAccessory() {
    UsbProbeResult result{UsbProbeState::Failed, "AOA re-enumeration", "The phone did not switch to Android accessory mode."};
    result.isRetryable = true;
    return result;
}
UsbProbeResult WrongDriver() {
    UsbProbeResult result{UsbProbeState::DriverUnavailable, "USB open", "LIBUSB_ERROR_NOT_FOUND"};
    result.canRepairDriver = true;
    return result;
}
// Scripted world: connect() consumes `connects` in order; every other dependency is counted.
struct Script {
    std::vector<UsbProbeResult> connects;
    std::vector<UsbScanResult> scans;    // the last entry repeats
    RepairResult repair{RepairOutcome::Fixed, "Treiber repariert."};
    RepairResult recover{RepairOutcome::Fixed, "Handy neu gestartet und Treiber repariert."};
    std::atomic_bool* stopDuringConnect{};
    int connectCalls{}, scanCalls{}, repairCalls{}, recoverCalls{};
    AutoConnectDeps Deps() {
        AutoConnectDeps deps;
        deps.scan = [this] { return scans[std::min<std::size_t>(scanCalls++, scans.size() - 1)]; };
        deps.connect = [this](const UsbDevice&) {
            if (stopDuringConnect) *stopDuringConnect = true;
            return connects.at(std::min<std::size_t>(connectCalls++, connects.size() - 1));
        };
        deps.repair = [this] { ++repairCalls; return repair; };
        deps.recover = [this] { ++recoverCalls; return recover; };
        deps.wait = [](std::chrono::milliseconds) {};
        return deps;
    }
};
UsbScanResult WithPhone(UsbDevice device = Phone()) { UsbScanResult scan; scan.devices.push_back(std::move(device)); return scan; }
Logger& TestLogger() {
    static Logger logger(std::filesystem::temp_directory_path() / "headunit-autoconnect-tests.log");
    return logger;
}
AutoConnectResult Run(Script& script, std::atomic_bool& stop) {
    return RunAutoConnect(script.Deps(), TestLogger(), stop, {});
}
}

void TestAutoConnect() {
    std::atomic_bool stop{false};
    {   // Everything works at once.
        Script script{{Video()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(result.hasVideo && script.connectCalls == 1 && script.repairCalls == 0 && script.recoverCalls == 0, "Straight connect took extra steps");
    }
    {   // Windows reverted the driver: repair once, then continue on its own.
        Script script{{WrongDriver(), Video()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(result.hasVideo && script.repairCalls == 1 && script.connectCalls == 2, "Driver repair did not lead to a connection");
    }
    {   // The phone does not answer: restart its USB connection once, wait for it to come back, connect again.
        UsbScanResult none;
        Script script{{NoAnswer(), Video()}, {WithPhone(Phone(0x18d1, 0x2d00)), none, WithPhone()}};
        const auto result = Run(script, stop);
        Check(result.hasVideo && script.recoverCalls == 1 && script.connectCalls == 2, "Recovery did not restore the connection");
        Check(script.scanCalls == 3, "Did not wait for the phone to re-enumerate after the recovery");
    }
    {   // The restarted phone still shows Samsung's driver: recover, repair, connect.
        Script script{{NoAnswer(), WrongDriver(), Video()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(result.hasVideo && script.recoverCalls == 1 && script.repairCalls == 1 && script.connectCalls == 3, "Recovery followed by driver repair failed");
    }
    {   // Nothing helps: a bounded number of recoveries, then a message the user can act on.
        Script script{{NoAnswer()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.recoverCalls == 2 && script.connectCalls == 3, "Recovery is not bounded");
        Check(result.message.find("entsperren") != std::string::npos, "Final message gives no hint");
    }
    {   // The phone did not switch to accessory mode (locked?): plain retries, no USB restart, no admin prompt.
        Script script{{NoAccessory(), Video()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(result.hasVideo && script.connectCalls == 2 && script.recoverCalls == 0 && script.repairCalls == 0, "A missed mode switch was not simply retried");
    }
    {   // ... and a bounded number of retries with advice the user can act on.
        Script script{{NoAccessory()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.connectCalls == 3 && script.recoverCalls == 0, "Retries of a missed mode switch are not bounded");
        Check(result.message.find("entsperren") != std::string::npos, "No hint for a phone that does not switch modes");
    }
    {   // The recovery is refused (e.g. UAC declined): say so, do not loop.
        Script script{{NoAnswer()}, {WithPhone()}};
        script.recover = {RepairOutcome::Cancelled, "Die Administrator-Abfrage wurde abgelehnt."};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.connectCalls == 1 && result.message.find("Kabel") != std::string::npos, "Failed recovery not reported");
    }
    {   // The user declines the administrator prompt: no retry loop.
        Script script{{WrongDriver()}, {WithPhone()}};
        script.repair = {RepairOutcome::Cancelled, "Die Administrator-Abfrage wurde abgelehnt."};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.repairCalls == 1 && script.connectCalls == 1, "Declined repair kept trying");
    }
    {   // A repair that never sticks stops after a few tries.
        Script script{{WrongDriver()}, {WithPhone()}};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.repairCalls == 3, "Endless driver repair");
    }
    {   // No phone attached.
        Script script{{Video()}, {UsbScanResult{}}};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.connectCalls == 0 && script.scanCalls == 10 && result.message.find("Kein Android-Handy") != std::string::npos, "Missing phone not reported");
    }
    {   // Two Android devices: refuse to guess.
        UsbScanResult two = WithPhone();
        two.devices.push_back(Phone(0x18d1, 0x2d00));
        Script script{{Video()}, {two}};
        const auto result = Run(script, stop);
        Check(!result.hasVideo && script.connectCalls == 0 && result.message.find("Mehrere") != std::string::npos, "Ambiguous devices accepted");
    }
    {   // Windows shows an administrator prompt for repair and restart, and the steps say so; elsewhere they must not
        // ask the user to confirm a prompt that never comes.
        for (const bool hasPrompt : {false, true}) {
            Script script{{NoAnswer(), WrongDriver(), Video()}, {WithPhone()}};
            auto deps = script.Deps();
            deps.needsAdminPrompt = hasPrompt;
            std::string steps;
            const auto result = RunAutoConnect(deps, TestLogger(), stop, [&](const std::string& step) { steps += step + '\n'; });
            Check(result.hasVideo && script.recoverCalls == 1 && script.repairCalls == 1, "The flow with an administrator prompt did not connect");
            Check((steps.find("Administratorrechte") != std::string::npos) == hasPrompt, "The administrator prompt is mentioned wrongly");
            Check(steps.find("Starte die USB-Verbindung") != std::string::npos && steps.find("Repariere ihn") != std::string::npos, "Repair and restart steps are missing");
        }
    }
    {   // Stop pressed while the session starts: no recovery steps afterwards.
        std::atomic_bool userStop{false};
        Script script{{NoAnswer()}, {WithPhone()}};
        script.stopDuringConnect = &userStop;
        const auto result = Run(script, userStop);
        Check(result.isStoppedByUser && script.recoverCalls == 0 && script.connectCalls == 1, "Stop did not end the flow");
    }
}
