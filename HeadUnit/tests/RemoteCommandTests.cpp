#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/ProjectionKeys.h"
#include "remote/RemoteCommand.h"
#include <string>
#include <utility>
#include <vector>

using namespace headunit;

namespace {
// A RemoteCommand whose effects are written down.
struct Recorder {
    std::vector<ConsoleKey> consoleKeys;
    std::vector<std::pair<unsigned, bool>> keys;
    std::vector<int> rotations;
    std::vector<int> volumeChanges;
    int muteToggles{};
    RemoteStatus status{15, false, "radio_home", false};
    RemoteCommand command{MakeDeps()};

    RemoteCommandDeps MakeDeps()
    {
        RemoteCommandDeps deps;
        deps.pressConsole = [this](ConsoleKey key) { consoleKeys.push_back(key); };
        deps.sendKey = [this](unsigned keycode, bool isDown) { keys.emplace_back(keycode, isDown); };
        deps.rotate = [this](int detents) { rotations.push_back(detents); };
        deps.changeVolume = [this](int delta) { volumeChanges.push_back(delta); };
        deps.toggleMute = [this] { ++muteToggles; };
        deps.readStatus = [this] { return status; };
        return deps;
    }
};

// Every console key name reaches the console with the right key.
void TestConsoleKeys()
{
    Recorder recorder;
    const std::pair<const char*, ConsoleKey> names[] = {{"home", ConsoleKey::Home}, {"menu", ConsoleKey::Menu}, {"option", ConsoleKey::Option},
        {"media", ConsoleKey::Media}, {"radio", ConsoleKey::Radio}, {"tel", ConsoleKey::Tel}, {"nav", ConsoleKey::Nav}, {"map", ConsoleKey::Map},
        {"back", ConsoleKey::Back}, {"projection", ConsoleKey::Projection}};
    for (const auto& [name, key] : names) {
        const auto reply = recorder.command.Execute(std::string("{\"cmd\":\"console\",\"key\":\"") + name + "\"}");
        Check(reply == "{\"ok\":true}", std::string("Console key ") + name + " was not accepted: " + reply);
        Check(recorder.consoleKeys.back() == key, std::string("Console key ") + name + " reached the wrong key");
    }
    Check(recorder.consoleKeys.size() == std::size(names), "A console key was pressed more or less than once");
}

// A media key or a knob action is a press followed by a release of the same key.
void TestKeyPresses()
{
    Recorder recorder;
    Check(recorder.command.Execute(R"({"cmd":"key","name":"play_pause"})") == "{\"ok\":true}", "play_pause was not accepted");
    Check(recorder.keys == (std::vector<std::pair<unsigned, bool>>{{keys::MediaPlayPause, true}, {keys::MediaPlayPause, false}}),
        "play_pause was not a press and a release");
    recorder.keys.clear();
    recorder.command.Execute(R"({"cmd":"key","name":"previous"})");
    recorder.command.Execute(R"({"cmd":"key","name":"next"})");
    Check(recorder.keys.size() == 4 && recorder.keys[0].first == keys::MediaPrevious && recorder.keys[2].first == keys::MediaNext, "previous and next were wrong");
    recorder.keys.clear();
    const std::pair<const char*, unsigned> actions[] = {{"press", keys::DpadCenter}, {"up", keys::DpadUp}, {"down", keys::DpadDown},
        {"left", keys::DpadLeft}, {"right", keys::DpadRight}};
    for (const auto& [action, keycode] : actions) {
        recorder.keys.clear();
        const auto reply = recorder.command.Execute(std::string("{\"cmd\":\"knob\",\"action\":\"") + action + "\"}");
        Check(reply == "{\"ok\":true}", std::string("Knob action ") + action + " was not accepted");
        Check(recorder.keys.size() == 2 && recorder.keys[0] == std::make_pair(keycode, true) && recorder.keys[1] == std::make_pair(keycode, false),
            std::string("Knob action ") + action + " was not a press and a release of its key");
    }
}

// Turning, volume and mute reach their effects; the numbers must be whole and in range.
void TestRotateVolumeMute()
{
    Recorder recorder;
    Check(recorder.command.Execute(R"({"cmd":"rotate","detents":-2})") == "{\"ok\":true}" && recorder.rotations == std::vector<int>{-2}, "rotate -2 failed");
    Check(recorder.command.Execute(R"({"cmd":"volume","delta":1})") == "{\"ok\":true}" && recorder.volumeChanges == std::vector<int>{1}, "volume +1 failed");
    Check(recorder.command.Execute(R"({"cmd":"mute"})") == "{\"ok\":true}" && recorder.muteToggles == 1, "mute failed");
    for (const char* bad : {R"({"cmd":"rotate","detents":0})", R"({"cmd":"rotate","detents":21})", R"({"cmd":"rotate","detents":1.5})",
             R"({"cmd":"rotate","detents":"2"})", R"({"cmd":"rotate"})", R"({"cmd":"volume","delta":0})", R"({"cmd":"volume","delta":31})",
             R"({"cmd":"volume","delta":99999999999999999999})"}) {
        Check(recorder.command.Execute(bad).rfind("{\"ok\":false,\"error\":\"", 0) == 0, std::string("This was accepted: ") + bad);
    }
    Check(recorder.rotations.size() == 1 && recorder.volumeChanges.size() == 1, "A refused command still had an effect");
}

// The status names the volume, the mute state, the page and whether a phone is projected.
void TestStatus()
{
    Recorder recorder;
    recorder.status = {7, true, "projection", true};
    Check(recorder.command.Execute(R"({"cmd":"status"})") == R"({"ok":true,"volume":7,"muted":true,"page":"projection","projecting":true})",
        "The status reply was wrong");
    Check(std::string(RemotePageName(ConsoleController::Screen::RadioHome)) == "radio_home" &&
        std::string(RemotePageName(ConsoleController::Screen::ProjectionHome)) == "projection_home" &&
        std::string(RemotePageName(ConsoleController::Screen::Bluetooth)) == "bluetooth", "A page name was wrong");
}

// Broken lines, unknown commands and unknown names are answered with an error and have no effect; spaces, key order and
// unknown extra fields do not matter.
void TestRefusedLines()
{
    Recorder recorder;
    for (const char* bad : {"", "hello", "[]", "{", R"({"cmd":)", R"({"cmd":"home"})", R"({"cmd":"console"})", R"({"cmd":"console","key":"nope"})",
             R"({"cmd":"console","key":5})", R"({"cmd":"key","name":"stop"})", R"({"cmd":"knob","action":"spin"})", R"({"cmd":"mute"} trailing)",
             R"({"cmd":"mute","nested":{"a":1}})", R"({"cmd":"mute","cmd":"mute"})"}) {
        Check(recorder.command.Execute(bad).rfind("{\"ok\":false,\"error\":\"", 0) == 0, std::string("This was accepted: ") + bad);
    }
    Check(recorder.consoleKeys.empty() && recorder.keys.empty() && recorder.muteToggles == 0, "A refused line had an effect");
    Check(recorder.command.Execute(R"(  { "key" : "back" , "extra" : true , "cmd" : "console" }  )") == "{\"ok\":true}", "Spaces and key order mattered");
    Check(recorder.consoleKeys.size() == 1 && recorder.consoleKeys[0] == ConsoleKey::Back, "The tolerant line did not press Back");
    const auto escaped = recorder.command.Execute(R"({"cmd":"console","key":"a\"b\\c"})");
    Check(escaped == R"({"ok":false,"error":"unknown console key"})", "An error echoed the input or was not JSON: " + escaped);
}
}

// Runs the remote command tests.
void RunRemoteCommandTests()
{
    TestConsoleKeys();
    TestKeyPresses();
    TestRotateVolumeMute();
    TestStatus();
    TestRefusedLines();
}
