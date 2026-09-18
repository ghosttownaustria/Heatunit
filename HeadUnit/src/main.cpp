#include "logging/Logger.h"
#include "usb/WindowsUsbBackend.h"
#include "usb/AndroidUsbProbe.h"
#include "usb/DriverRepair.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "ui/MainWindow.h"
#include <QApplication>
#include <filesystem>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[])
{
    bool isScanOnly = false, isSmokeTest = false, isUsbProbe = false, isStartAccessory = false, isProjectionTest = false, isRepairDriver = false, isRecoverPhone = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--scan") isScanOnly = true;
        else if (argument == "--repair-driver") isRepairDriver = true;
        else if (argument == "--recover-phone") isRecoverPhone = true;
        else if (argument == "--smoke-test") isSmokeTest = true;
        else if (argument == "--probe-usb") isUsbProbe = true;
        else if (argument == "--start-accessory") isStartAccessory = true;
        else if (argument == "--test-projection") isProjectionTest = true;
        else if (argument == "--help") {
            std::cout << "HeadUnit [--scan | --smoke-test | --probe-usb | --start-accessory | --test-projection | --repair-driver | --recover-phone]\n"
                         "Logs: ./headunit.log (includes USB serial numbers)\n"
                         "--repair-driver rebinds the phone to WinUSB (needs administrator rights; the app starts it itself when needed)\n";
            return 0;
        } else { std::cerr << "Unknown option: " << argument << '\n'; return 1; }
    }
    if (static_cast<int>(isScanOnly) + static_cast<int>(isSmokeTest) + static_cast<int>(isUsbProbe) + static_cast<int>(isStartAccessory) + static_cast<int>(isProjectionTest) + static_cast<int>(isRepairDriver) + static_cast<int>(isRecoverPhone) > 1) { std::cerr << "Choose one run mode\n"; return 1; }
    try {
        if (isRepairDriver || isRecoverPhone) {
            // Runs elevated in its own process, so it keeps a separate log.
            headunit::Logger repairLog(std::filesystem::current_path() / "headunit-repair.log");
            auto result = isRecoverPhone ? headunit::RecoverPhoneNow(repairLog) : headunit::RepairPhoneDriverNow(repairLog);
            // Started from a normal console: ask Windows for administrator rights (UAC) and repeat there.
            if (result.outcome == headunit::RepairOutcome::NotElevated)
                result = isRecoverPhone ? headunit::RecoverPhoneElevated(repairLog) : headunit::RepairPhoneDriverElevated(repairLog);
            std::cout << result.message << '\n';
            return static_cast<int>(result.outcome);
        }
        headunit::Logger logger(std::filesystem::current_path() / "headunit.log");
        logger.Write("INFO", "APP", "HeadUnit 0.2.0 started; USB Android Auto projection; log includes serial numbers");
        headunit::WindowsUsbBackend backend(logger);
        if (isScanOnly) return backend.EnumerateDevices().errors.empty() ? 0 : 2;
        if (isUsbProbe || isStartAccessory) {
            const auto scan = backend.EnumerateDevices();
            std::vector<headunit::UsbDevice> candidates;
            for (const auto& device : scan.devices)
                if (headunit::DetectAndroidDevice(device).evidence != headunit::AndroidEvidence::None) candidates.push_back(device);
            if (!scan.errors.empty() || candidates.size() != 1) {
                logger.Write("ERROR", "AA", "Probe requires a complete scan with exactly one Android candidate. Use the UI to select a device.");
                return 3;
            }
            const auto result = isStartAccessory ? headunit::StartAndroidAccessory(candidates.front(), logger) : headunit::ProbeAndroidUsb(candidates.front(), logger);
            return result.state == headunit::UsbProbeState::AoaAvailable || result.state == headunit::UsbProbeState::AccessoryAvailable ||
                result.state == headunit::UsbProbeState::AccessoryTransportReady ? 0 : 3;
        }
        QApplication application(argc, argv);
        headunit::MainWindow window(backend, logger, isSmokeTest, isProjectionTest);
        window.show();
        logger.Write("INFO", "UI", "Qt " QT_VERSION_STR " window initialized");
        const auto result = application.exec();
        logger.Write("INFO", "APP", "Event loop stopped");
        return result;
    } catch (const std::exception& error) {
        std::cerr << "[FATAL][APP] " << error.what() << '\n';
        return 1;
    }
}
