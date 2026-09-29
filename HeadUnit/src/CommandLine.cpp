#include "CommandLine.h"
#include <utility>

namespace headunit {
namespace {
// An option that selects a run mode, and whether it exists only with wireless Android Auto.
struct ModeOption {
    std::string_view name;
    RunMode mode;
    bool isWirelessOnly;
};

constexpr ModeOption kModeOptions[] = {
    {"--scan", RunMode::Scan, false},
    {"--smoke-test", RunMode::SmokeTest, false},
    {"--probe-usb", RunMode::ProbeUsb, false},
    {"--start-accessory", RunMode::StartAccessory, false},
    {"--test-projection", RunMode::TestProjection, false},
    {"--test-input", RunMode::TestInput, false},
    {"--test-audio", RunMode::TestAudio, false},
    {"--test-console", RunMode::TestConsole, false},
    {"--test-keys", RunMode::TestKeys, false},
    {"--test-tone", RunMode::TestTone, false},
    {"--repair-driver", RunMode::RepairDriver, false},
    {"--recover-phone", RunMode::RecoverPhone, false},
    {"--test-bluetooth", RunMode::TestBluetooth, true},
    {"--test-hotspot", RunMode::TestHotspot, true},
};

// The mode option called `name`, if this build has it.
const ModeOption* FindModeOption(std::string_view name, bool hasWireless)
{
    for (const auto& option : kModeOptions) {
        if (option.name == name && (hasWireless || !option.isWirelessOnly)) return &option;
    }
    return nullptr;
}

// The message for a --display without a valid size.
std::string DisplayError()
{
    std::string text = "--display needs one of these sizes:";
    for (const auto& known : kDisplays) text += " " + std::to_string(known.width) + "x" + std::to_string(known.height);
    return text;
}
}

// A TCP port number from 0 to 65535, or nothing for any other text.
std::optional<int> ParsePort(std::string_view text)
{
    if (text.empty() || text.size() > 5) return std::nullopt;
    int port = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        port = port * 10 + (c - '0');
    }
    if (port > 65535) return std::nullopt;
    return port;
}

// Reads the program's arguments (without the program name). At most one run mode may be chosen; --wireless counts as
// one. --help ends the reading. The wireless options exist only when `hasWireless`.
CommandLine ParseCommandLine(const std::vector<std::string_view>& arguments, bool hasWireless)
{
    CommandLine commandLine;
    int modeCount = 0;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--help") {
            commandLine.isHelpRequested = true;
            return commandLine;
        }
        if (argument == "--display") {
            commandLine.display = index + 1 < arguments.size() ? ParseDisplay(arguments[index + 1]) : std::nullopt;
            if (!commandLine.display) {
                commandLine.error = DisplayError();
                return commandLine;
            }
            ++index;
        } else if (argument == "--api-port") {
            commandLine.apiPort = index + 1 < arguments.size() ? ParsePort(arguments[index + 1]) : std::nullopt;
            if (!commandLine.apiPort) {
                commandLine.error = "--api-port needs a port number from 0 to 65535";
                return commandLine;
            }
            ++index;
        } else if (argument == "--wireless" && hasWireless) {
            commandLine.isWirelessRequested = true;
            ++modeCount;
        } else if (const ModeOption* option = FindModeOption(argument, hasWireless)) {
            commandLine.mode = option->mode;
            ++modeCount;
        } else {
            commandLine.error = "Unknown option: " + std::string(argument);
            return commandLine;
        }
    }
    if (modeCount > 1) commandLine.error = "Choose one run mode";
    return commandLine;
}

// The text of --help.
std::string CommandLineHelp(bool hasWireless)
{
    std::string text =
        "HeadUnit [--scan | --smoke-test | --probe-usb | --start-accessory | --test-projection | --test-input | --test-audio | --test-console | "
        "--test-keys | --test-tone | --repair-driver | --recover-phone] [--display 800x480|1280x720|1600x600|1920x1080]\n";
    if (hasWireless) {
        text +=
            "HeadUnit [--test-bluetooth | --test-hotspot]\n"
            "Without an option the window watches USB and wireless Android Auto from the start (Wi-Fi hotspot + Bluetooth, docs/wireless.md); "
            "--wireless is accepted and changes nothing\n"
            "--test-bluetooth makes the computer visible as HEATUNIT and checks that a phone pairs and asks for the Wi-Fi details "
            "(HEADUNIT_TEST_SECONDS, default 120)\n"
            "--test-hotspot starts the Wi-Fi hotspot for a while and prints how to join it (HEADUNIT_TEST_SECONDS, default 60)\n";
    }
    text +=
        "Logs: ./headunit.log (includes USB serial numbers); in the user's state directory when the working directory is not writable.\n"
        "      HEADUNIT_LOG_LEVEL=trace|debug|info|warning|error overrides the detail the build profile chose\n"
        "--api-port picks the TCP port of the remote API on 127.0.0.1 (default 47050, HEADUNIT_API_PORT also sets it, 0 switches it off; docs/api.md)\n"
        "--display picks the display size for this run (the window's own choice is remembered, this one is not)\n"
        "--test-tone plays a short quiet tone through the audio output, which needs no phone\n"
        "--repair-driver Windows: rebinds the phone to WinUSB (needs administrator rights; the app starts it itself when needed)\n"
        "                Linux: checks that the phone can be opened and says what to install when it cannot (docs/linux.md)\n";
    return text;
}
}
