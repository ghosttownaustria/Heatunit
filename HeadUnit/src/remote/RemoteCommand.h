#pragma once
#include "androidauto/ConsoleController.h"
#include <functional>
#include <string>

namespace headunit {
// What the status command reports.
struct RemoteStatus {
    int volume{};
    bool isMuted{};
    std::string page;
    bool isProjecting{};
};

// What the remote commands do, supplied by the window: the same actions the simulated console triggers.
struct RemoteCommandDeps {
    std::function<void(ConsoleKey key)> pressConsole;
    std::function<void(unsigned keycode, bool isDown)> sendKey;
    std::function<void(int detents)> rotate;
    std::function<void(int delta)> changeVolume;
    std::function<void()> toggleMute;
    std::function<RemoteStatus()> readStatus;
};

// The name of a screen in the status reply.
const char* RemotePageName(ConsoleController::Screen screen);

// Runs the commands other programs send to the head unit (docs/api.md): one JSON object per line in, one JSON object per
// line out. A line that is not understood is answered with an error and changes nothing. Used from the GUI thread only.
class RemoteCommand {
public:
    explicit RemoteCommand(RemoteCommandDeps deps);

    std::string Execute(const std::string& line);

private:
    RemoteCommandDeps m_deps;
};
}
