#pragma once
#include "androidauto/DisplayConfig.h"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace headunit {
// What one run of the program does. Window is the normal car mode; the others are diagnostics that mostly need no window.
enum class RunMode {
    Window, Scan, SmokeTest, ProbeUsb, StartAccessory, TestProjection, TestInput, TestAudio, TestConsole, TestKeys, TestTone,
    RepairDriver, RecoverPhone, TestBluetooth, TestHotspot,
};

// The program's arguments, as ParseCommandLine understood them.
struct CommandLine {
    RunMode mode{RunMode::Window};
    std::optional<DisplayConfig> display;
    bool isWirelessRequested{};   // --wireless: accepted for old launchers, wireless Android Auto runs anyway
    bool isHelpRequested{};
    std::string error;            // why the arguments were refused; empty when they were accepted
};

CommandLine ParseCommandLine(const std::vector<std::string_view>& arguments, bool hasWireless);
std::string CommandLineHelp(bool hasWireless);
}
