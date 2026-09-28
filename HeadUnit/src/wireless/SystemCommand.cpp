#include "wireless/SystemCommand.h"
#include "wireless/FileDescriptor.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace headunit {
namespace {
// The environment of this process with untranslated messages (LC_ALL=C, no LANG).
std::vector<std::string> UntranslatedEnvironment()
{
    std::vector<std::string> environment;
    for (char** entry = environ; entry && *entry; ++entry) {
        if (std::strncmp(*entry, "LC_ALL=", 7) != 0 && std::strncmp(*entry, "LANG=", 5) != 0) environment.emplace_back(*entry);
    }
    environment.emplace_back("LC_ALL=C");
    return environment;
}

// The pointers exec wants: every string of `texts`, then a null.
std::vector<char*> PointerList(std::vector<std::string>& texts)
{
    std::vector<char*> pointers;
    for (auto& text : texts) pointers.push_back(text.data());
    pointers.push_back(nullptr);
    return pointers;
}

// Reads everything the child writes until it closes the pipe or `timeout` is over; true when the time ran out.
bool ReadOutput(int reader, std::chrono::milliseconds timeout, std::string& output)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char buffer[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) return true;
        pollfd waiting{reader, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return false;
        if (ready == 0) continue;
        const auto got = ::read(reader, buffer, sizeof(buffer));
        if (got > 0) {
            output.append(buffer, static_cast<std::size_t>(got));
            continue;
        }
        if (got < 0 && errno == EINTR) continue;
        return false;   // end of the output, or an error
    }
}
}

// Runs a program without a shell, so that nothing in the arguments (an SSID, a password) is ever interpreted. The output
// is untranslated (LC_ALL=C). A program that runs longer than `timeout` is killed.
CommandResult RunCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout)
{
    CommandResult result;
    if (arguments.empty()) return result;
    int pipeEnds[2];
    if (::pipe2(pipeEnds, O_CLOEXEC) != 0) return result;
    const FileDescriptor reader(pipeEnds[0]);
    FileDescriptor writer(pipeEnds[1]);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, writer.Get(), STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, writer.Get(), STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    std::vector<std::string> argumentTexts(arguments);
    std::vector<std::string> environment = UntranslatedEnvironment();
    const std::vector<char*> argv = PointerList(argumentTexts);
    const std::vector<char*> envp = PointerList(environment);
    pid_t child = 0;
    const int spawned = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return result;   // e.g. ENOENT: not installed
    result.isStarted = true;
    writer.Reset();   // the child holds the only writing end now; the end of the output arrives when it exits

    result.isTimedOut = ReadOutput(reader.Get(), timeout, result.output);
    if (result.isTimedOut) ::kill(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (!result.isTimedOut && WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    return result;
}
}
