#include "platform/ShutdownSignal.h"
#include <atomic>
#include <csignal>

namespace headunit {
namespace {
std::atomic_bool isShutdownRequested{false};

// Signal handler: only notes the request; the window polls it.
[[maybe_unused]] void NoteShutdownRequest(int)
{
    isShutdownRequested = true;
}
}

// Linux: Ctrl+C (SIGINT) and a service stop (SIGTERM) ask the program to close as if the window's close button was
// pressed, which ends a running session and takes the hotspot down. Windows keeps the console's default behaviour.
void InstallShutdownSignalHandlers()
{
#ifndef _WIN32
    std::signal(SIGINT, NoteShutdownRequest);
    std::signal(SIGTERM, NoteShutdownRequest);
#endif
}

// Whether a shutdown was asked for since the last call.
bool ConsumeShutdownRequest()
{
    return isShutdownRequested.exchange(false);
}
}
