#include "CommandLine.h"
#include "androidauto/AndroidDeviceDetector.h"
#include "audio/AudioEngine.h"
#include "logging/Logger.h"
#include "platform/BuildProfile.h"
#include "platform/Environment.h"
#include "platform/ShutdownSignal.h"
#include "ui/MainWindow.h"
#include "usb/AndroidUsbProbe.h"
#include "usb/DriverRepair.h"
#include "usb/UsbBackendFactory.h"
#ifdef HEADUNIT_WIRELESS
#include "wireless/Hotspot.h"
#include "wireless/WirelessDiagnostics.h"
#endif
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using namespace headunit;
#ifdef HEADUNIT_WIRELESS
constexpr bool kHasWireless = true;
#else
constexpr bool kHasWireless = false;
#endif
constexpr int kCloseCheckIntervalMs = 200;

// Plays two seconds of a quiet 440 Hz tone through this platform's audio engine, at the pace of a live stream: a check
// of the sound output that needs no phone. Succeeds when the output device took most of the audio.
int RunToneTest(Logger& logger)
{
    using namespace std::chrono_literals;
    constexpr PcmFormat kFormat{48000, 2, 16};
    constexpr int kSliceMilliseconds = 10;
    constexpr int kSlices = 200;
    constexpr double kPi = 3.14159265358979323846;
    auto state = std::make_shared<AudioState>();
    AudioEngine engine(state, logger);
    const auto output = engine.Opener()(AudioKind::Media, kFormat);
    if (!output) {
        logger.Write(LogLevel::Error, "TEST", "Tone test: the audio output could not be opened");
        return 4;
    }
    const std::size_t sliceFrames = kFormat.sampleRate * kSliceMilliseconds / 1000;
    std::vector<std::int16_t> samples(sliceFrames * kFormat.channels);
    double phase = 0;
    auto next = std::chrono::steady_clock::now();
    for (int slice = 0; slice < kSlices; ++slice) {
        for (std::size_t frame = 0; frame < sliceFrames; ++frame, phase += 2 * kPi * 440 / kFormat.sampleRate) {
            for (std::uint32_t channel = 0; channel < kFormat.channels; ++channel)
                samples[frame * kFormat.channels + channel] = static_cast<std::int16_t>(std::sin(phase) * 10000);
        }
        output->Write({reinterpret_cast<const std::uint8_t*>(samples.data()), samples.size() * sizeof(std::int16_t)});
        next += std::chrono::milliseconds(kSliceMilliseconds);
        std::this_thread::sleep_until(next);
    }
    std::this_thread::sleep_for(300ms);   // the last audio is still queued
    const auto played = state->BytesRendered(AudioKind::Media);
    const auto expected = kFormat.BytesPerSecond() * 3 / 2;
    const bool isPlayed = played >= expected;
    logger.Write(isPlayed ? LogLevel::Info : LogLevel::Error, "TEST", "Tone test: " + std::to_string(played) + " of " + std::to_string(kFormat.BytesPerSecond() * 2) +
        " bytes reached the audio output" + (isPlayed ? "" : "; is a sound system running and an output device selected?"));
    return isPlayed ? 0 : 5;
}

// --repair-driver and --recover-phone. On Windows this runs elevated in its own process, so it keeps a separate log;
// started from a normal console it asks Windows for administrator rights (UAC) and repeats there. The exit code is the
// RepairOutcome.
int RunRepair(bool isRecover)
{
    Logger repairLog(DefaultLogPath("headunit-repair.log"));
    auto result = isRecover ? RecoverPhoneNow(repairLog) : RepairPhoneDriverNow(repairLog);
    if (result.outcome == RepairOutcome::NotElevated) result = isRecover ? RecoverPhoneElevated(repairLog) : RepairPhoneDriverElevated(repairLog);
    std::cout << result.message << '\n';
    return static_cast<int>(result.outcome);
}

// --probe-usb and --start-accessory: the one Android device on the bus is probed (or switched to accessory mode).
int RunUsbProbe(IUsbBackend& backend, Logger& logger, bool isStartAccessory)
{
    const auto scan = backend.EnumerateDevices();
    std::vector<UsbDevice> candidates;
    for (const auto& device : scan.devices) {
        if (DetectAndroidDevice(device).evidence != AndroidEvidence::None) candidates.push_back(device);
    }
    if (!scan.errors.empty() || candidates.size() != 1) {
        logger.Write(LogLevel::Error, "AA", "Probe requires a complete scan with exactly one Android candidate. Use the UI to select a device.");
        return 3;
    }
    const auto result = isStartAccessory ? StartAndroidAccessory(candidates.front(), logger) : ProbeAndroidUsb(candidates.front(), logger);
    return IsSuccessfulProbe(result.state) ? 0 : 3;
}

#ifdef HEADUNIT_WIRELESS
// --test-bluetooth and --test-hotspot. D-Bus (Bluetooth) delivers BlueZ's calls as Qt events, so the tests need an
// application object, but no window. HEADUNIT_TEST_SECONDS sets how long they run.
int RunWirelessTest(int argc, char* argv[], Logger& logger, RunMode mode)
{
    QCoreApplication core(argc, argv);
    const bool isBluetooth = mode == RunMode::TestBluetooth;
    int seconds = isBluetooth ? 120 : 60;
    if (const auto text = GetEnv("HEADUNIT_TEST_SECONDS")) {
        try {
            seconds = std::max(5, std::stoi(*text));
        } catch (const std::exception&) {
        }
    }
    return isBluetooth ? RunBluetoothTest(logger, std::chrono::seconds(seconds)) : RunHotspotTest(logger, std::chrono::seconds(seconds));
}
#endif

// The window's test mode for a run mode.
MainWindow::TestMode TestModeOf(RunMode mode)
{
    switch (mode) {
    case RunMode::SmokeTest: return MainWindow::TestMode::Smoke;
    case RunMode::TestProjection: return MainWindow::TestMode::Projection;
    case RunMode::TestInput: return MainWindow::TestMode::Input;
    case RunMode::TestAudio: return MainWindow::TestMode::Audio;
    case RunMode::TestConsole: return MainWindow::TestMode::Console;
    case RunMode::TestKeys: return MainWindow::TestMode::Keys;
    default: return MainWindow::TestMode::None;
    }
}

// Whether the window is the car's full-screen one: in a release build, for the car mode (and the smoke test, which shows
// what the car shows). HEADUNIT_KIOSK=1 / 0 switches it on / off in any build, for development.
bool IsCarWindow(MainWindow::TestMode mode)
{
    if (mode != MainWindow::TestMode::None && mode != MainWindow::TestMode::Smoke) return false;
    if (const auto setting = GetEnv("HEADUNIT_KIOSK"); setting && !setting->empty()) return *setting != "0";
    return kIsKioskBuild;
}

// The window, in the car mode or one of its test modes, until it closes. Closing properly (also on Ctrl+C or a service
// stop) ends a running session and takes the wireless mode's hotspot down again.
int RunWindow(int argc, char* argv[], IUsbBackend& backend, Logger& logger, const CommandLine& commandLine)
{
#ifdef HEADUNIT_WIRELESS
    // A run that was killed cannot take its hotspot down; NetworkManager would keep it on the air until the next reboot.
    RemoveLeftoverHotspot(logger);
#endif
    QApplication application(argc, argv);
    const MainWindow::TestMode testMode = TestModeOf(commandLine.mode);
    MainWindow window(backend, logger, testMode);
    const bool isCarWindow = IsCarWindow(testMode);
    if (isCarWindow) window.EnterKioskMode();
    if (commandLine.display) window.SetDisplay(*commandLine.display);
    if (isCarWindow) window.showFullScreen();
    else window.show();
    if (commandLine.isWirelessRequested) logger.Write(LogLevel::Info, "APP", "--wireless: wireless Android Auto runs from the start anyway");
    InstallShutdownSignalHandlers();
    QTimer closeWatch;
    QObject::connect(&closeWatch, &QTimer::timeout, &window, [&window] {
        if (ConsumeShutdownRequest()) window.close();
    });
    closeWatch.start(kCloseCheckIntervalMs);
    logger.Write(LogLevel::Info, "UI", "Qt " QT_VERSION_STR " window initialized");
    const auto result = application.exec();
    logger.Write(LogLevel::Info, "APP", "Event loop stopped");
    return result;
}

// Runs the chosen mode with the program's log (the repair modes keep their own).
int Run(int argc, char* argv[], const CommandLine& commandLine)
{
    if (commandLine.mode == RunMode::RepairDriver || commandLine.mode == RunMode::RecoverPhone) return RunRepair(commandLine.mode == RunMode::RecoverPhone);
    Logger logger(DefaultLogPath("headunit.log"));
    logger.Write(LogLevel::Info, "APP", std::string("HeadUnit 0.2.0 started; automatic USB") + (kHasWireless ? " and wireless" : "") +
        " Android Auto projection; log includes serial numbers");
    if (commandLine.mode == RunMode::TestTone) return RunToneTest(logger);
#ifdef HEADUNIT_WIRELESS
    if (commandLine.mode == RunMode::TestBluetooth || commandLine.mode == RunMode::TestHotspot) return RunWirelessTest(argc, argv, logger, commandLine.mode);
#endif
    const auto backend = CreateUsbBackend(logger);
    if (commandLine.mode == RunMode::Scan) return backend->EnumerateDevices().errors.empty() ? 0 : 2;
    if (commandLine.mode == RunMode::ProbeUsb || commandLine.mode == RunMode::StartAccessory)
        return RunUsbProbe(*backend, logger, commandLine.mode == RunMode::StartAccessory);
    return RunWindow(argc, argv, *backend, logger, commandLine);
}
}

// Reads the arguments and runs the chosen mode; see CommandLineHelp for the modes.
int main(int argc, char* argv[])
{
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    const CommandLine commandLine = ParseCommandLine(arguments, kHasWireless);
    if (commandLine.isHelpRequested) {
        std::cout << CommandLineHelp(kHasWireless);
        return 0;
    }
    if (!commandLine.error.empty()) {
        std::cerr << commandLine.error << '\n';
        return 1;
    }
    // Every QSettings of the program uses these names.
    QCoreApplication::setOrganizationName("HeadUnit");
    QCoreApplication::setApplicationName("HeadUnit");
    try {
        return Run(argc, argv, commandLine);
    } catch (const std::exception& error) {
        std::cerr << "[FATAL][APP] " << error.what() << '\n';
        return 1;
    }
}
