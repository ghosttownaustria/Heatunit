#include "logging/Logger.h"
#include "usb/UsbBackendFactory.h"
#include "usb/AndroidUsbProbe.h"
#include "usb/DriverRepair.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "androidauto/DisplayConfig.h"
#include "audio/AudioEngine.h"
#include "ui/MainWindow.h"
#ifdef HEADUNIT_WIRELESS
#include "platform/Environment.h"
#include "wireless/WirelessConnect.h"
#include <QCoreApplication>
#endif
#include <QApplication>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <optional>
#include <string_view>
#include <thread>
#include <vector>

namespace {
// Plays two seconds of a quiet 440 Hz tone through this platform's audio engine, at the pace of a live stream:
// a check of the sound output that needs no phone. Succeeds when the output device took most of the audio.
int RunToneTest(headunit::Logger& logger)
{
    using namespace std::chrono_literals;
    constexpr headunit::PcmFormat format{48000, 2, 16};
    constexpr int kSliceMilliseconds = 10, kSlices = 200;
    constexpr double kPi = 3.14159265358979323846;
    auto state = std::make_shared<headunit::AudioState>();
    const auto engine = headunit::CreateAudioEngine(state, logger);
    const auto output = engine->Opener()(headunit::AudioKind::Media, format);
    if (!output) { logger.Write("ERROR", "TEST", "Tone test: the audio output could not be opened"); return 4; }
    const std::size_t sliceFrames = format.sampleRate * kSliceMilliseconds / 1000;
    std::vector<std::int16_t> samples(sliceFrames * format.channels);
    double phase = 0;
    auto next = std::chrono::steady_clock::now();
    for (int slice = 0; slice < kSlices; ++slice) {
        for (std::size_t frame = 0; frame < sliceFrames; ++frame, phase += 2 * kPi * 440 / format.sampleRate)
            for (std::uint32_t channel = 0; channel < format.channels; ++channel)
                samples[frame * format.channels + channel] = static_cast<std::int16_t>(std::sin(phase) * 10000);
        output->Write({reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size() * sizeof(std::int16_t)});
        next += std::chrono::milliseconds(kSliceMilliseconds);
        std::this_thread::sleep_until(next);
    }
    std::this_thread::sleep_for(300ms);   // the last audio is still queued
    const auto played = state->BytesRendered(headunit::AudioKind::Media);
    const auto expected = format.BytesPerSecond() * 3 / 2;
    logger.Write(played >= expected ? "INFO" : "ERROR", "TEST", "Tone test: " + std::to_string(played) + " of " + std::to_string(format.BytesPerSecond() * 2) +
        " bytes reached the audio output" + (played >= expected ? "" : "; is a sound system running and an output device selected?"));
    return played >= expected ? 0 : 5;
}
}

int main(int argc, char* argv[])
{
    bool isScanOnly = false, isSmokeTest = false, isUsbProbe = false, isStartAccessory = false, isProjectionTest = false, isRepairDriver = false, isRecoverPhone = false, isInputTest = false, isAudioTest = false, isConsoleTest = false, isKeysTest = false, isToneTest = false, isBluetoothTest = false, isHotspotTest = false, isWireless = false;
    std::optional<headunit::DisplayConfig> display;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--display") {
            display = index + 1 < argc ? headunit::ParseDisplay(argv[index + 1]) : std::nullopt;
            if (!display) {
                std::cerr << "--display needs one of these sizes:";
                for (const auto& known : headunit::kDisplays) std::cerr << ' ' << known.width << 'x' << known.height;
                std::cerr << '\n';
                return 1;
            }
            ++index;
        }
        else if (argument == "--scan") isScanOnly = true;
        else if (argument == "--repair-driver") isRepairDriver = true;
        else if (argument == "--recover-phone") isRecoverPhone = true;
        else if (argument == "--smoke-test") isSmokeTest = true;
        else if (argument == "--probe-usb") isUsbProbe = true;
        else if (argument == "--start-accessory") isStartAccessory = true;
        else if (argument == "--test-projection") isProjectionTest = true;
        else if (argument == "--test-input") isInputTest = true;
        else if (argument == "--test-audio") isAudioTest = true;
        else if (argument == "--test-console") isConsoleTest = true;
        else if (argument == "--test-keys") isKeysTest = true;
        else if (argument == "--test-tone") isToneTest = true;
#ifdef HEADUNIT_WIRELESS
        else if (argument == "--test-bluetooth") isBluetoothTest = true;
        else if (argument == "--test-hotspot") isHotspotTest = true;
        else if (argument == "--wireless") isWireless = true;
#endif
        else if (argument == "--help") {
            std::cout << "HeadUnit [--scan | --smoke-test | --probe-usb | --start-accessory | --test-projection | --test-input | --test-audio | --test-console | --test-keys | --test-tone | --repair-driver | --recover-phone] [--display 800x480|1280x720|1600x600|1920x1080]\n"
#ifdef HEADUNIT_WIRELESS
                         "HeadUnit [--wireless | --test-bluetooth | --test-hotspot]\n"
                         "--wireless starts wireless Android Auto right away (Wi-Fi hotspot + Bluetooth, docs/wireless.md)\n"
                         "--test-bluetooth makes the computer visible as HEATUNIT and checks that a phone pairs and asks for the Wi-Fi details (HEADUNIT_TEST_SECONDS, default 120)\n"
                         "--test-hotspot starts the Wi-Fi hotspot for a while and prints how to join it (HEADUNIT_TEST_SECONDS, default 60)\n"
#endif
                         "Logs: ./headunit.log (includes USB serial numbers); in the user's state directory when the working directory is not writable\n"
                         "--display picks the display size for this run (the window's own choice is remembered, this one is not)\n"
                         "--test-tone plays a short quiet tone through the audio output, which needs no phone\n"
                         "--repair-driver Windows: rebinds the phone to WinUSB (needs administrator rights; the app starts it itself when needed)\n"
                         "                Linux: checks that the phone can be opened and says what to install when it cannot (docs/linux.md)\n";
            return 0;
        } else { std::cerr << "Unknown option: " << argument << '\n'; return 1; }
    }
    if (static_cast<int>(isScanOnly) + static_cast<int>(isSmokeTest) + static_cast<int>(isUsbProbe) + static_cast<int>(isStartAccessory) + static_cast<int>(isProjectionTest) + static_cast<int>(isRepairDriver) + static_cast<int>(isRecoverPhone) + static_cast<int>(isInputTest) + static_cast<int>(isAudioTest) + static_cast<int>(isConsoleTest) + static_cast<int>(isKeysTest) + static_cast<int>(isToneTest) + static_cast<int>(isBluetoothTest) + static_cast<int>(isHotspotTest) + static_cast<int>(isWireless) > 1) { std::cerr << "Choose one run mode\n"; return 1; }
    try {
        if (isRepairDriver || isRecoverPhone) {
            // On Windows this runs elevated in its own process, so it keeps a separate log.
            headunit::Logger repairLog(headunit::DefaultLogPath("headunit-repair.log"));
            auto result = isRecoverPhone ? headunit::RecoverPhoneNow(repairLog) : headunit::RepairPhoneDriverNow(repairLog);
            // Started from a normal console: ask Windows for administrator rights (UAC) and repeat there.
            if (result.outcome == headunit::RepairOutcome::NotElevated)
                result = isRecoverPhone ? headunit::RecoverPhoneElevated(repairLog) : headunit::RepairPhoneDriverElevated(repairLog);
            std::cout << result.message << '\n';
            return static_cast<int>(result.outcome);
        }
        headunit::Logger logger(headunit::DefaultLogPath("headunit.log"));
        logger.Write("INFO", "APP", "HeadUnit 0.2.0 started; USB Android Auto projection; log includes serial numbers");
        if (isToneTest) return RunToneTest(logger);
#ifdef HEADUNIT_WIRELESS
        if (isBluetoothTest || isHotspotTest) {
            // D-Bus (Bluetooth) delivers BlueZ's calls as Qt events, so the tests need an application object, but no window.
            QCoreApplication core(argc, argv);
            int seconds = isBluetoothTest ? 120 : 60;
            if (const auto text = headunit::GetEnv("HEADUNIT_TEST_SECONDS")) { try { seconds = std::max(5, std::stoi(*text)); } catch (const std::exception&) {} }
            return isBluetoothTest ? headunit::RunBluetoothTest(logger, std::chrono::seconds(seconds)) : headunit::RunHotspotTest(logger, std::chrono::seconds(seconds));
        }
#else
        (void)isBluetoothTest; (void)isHotspotTest; (void)isWireless;
#endif
        const auto backendOwner = headunit::CreateUsbBackend(logger);
        headunit::IUsbBackend& backend = *backendOwner;
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
        using Mode = headunit::MainWindow::TestMode;
        headunit::MainWindow window(backend, logger, isSmokeTest ? Mode::Smoke : isProjectionTest ? Mode::Projection : isInputTest ? Mode::Input : isAudioTest ? Mode::Audio : isConsoleTest ? Mode::Console : isKeysTest ? Mode::Keys : Mode::None);
        if (display) window.SetDisplay(*display);
        window.show();
        if (isWireless) window.StartWirelessConnect();
        logger.Write("INFO", "UI", "Qt " QT_VERSION_STR " window initialized");
        const auto result = application.exec();
        logger.Write("INFO", "APP", "Event loop stopped");
        return result;
    } catch (const std::exception& error) {
        std::cerr << "[FATAL][APP] " << error.what() << '\n';
        return 1;
    }
}
