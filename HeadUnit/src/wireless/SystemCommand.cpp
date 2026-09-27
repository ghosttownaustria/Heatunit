#include "wireless/SystemCommand.h"
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
// Closes a file descriptor when it goes out of scope.
struct Descriptor {
    int fd{-1};
    ~Descriptor() { Close(); }
    void Close() { if (fd >= 0) { ::close(fd); fd = -1; } }
};
}

CommandResult RunCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout)
{
    CommandResult result;
    if (arguments.empty()) return result;

    int pipeEnds[2];
    if (::pipe2(pipeEnds, O_CLOEXEC) != 0) return result;
    Descriptor reader{pipeEnds[0]}, writer{pipeEnds[1]};

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, writer.fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, writer.fd, STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);

    std::vector<char*> argv;
    for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    std::vector<std::string> ownEnvironment;
    for (char** entry = environ; entry && *entry; ++entry)
        if (std::strncmp(*entry, "LC_ALL=", 7) != 0 && std::strncmp(*entry, "LANG=", 5) != 0) ownEnvironment.emplace_back(*entry);
    ownEnvironment.emplace_back("LC_ALL=C");
    std::vector<char*> envp;
    for (auto& entry : ownEnvironment) envp.push_back(entry.data());
    envp.push_back(nullptr);

    pid_t child = 0;
    const int spawned = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    if (spawned != 0) return result;   // e.g. ENOENT: not installed
    result.isStarted = true;
    writer.Close();   // the child holds the only writing end now; EOF arrives when it exits

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    char buffer[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) { result.isTimedOut = true; break; }
        pollfd waiting{reader.fd, POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (ready == 0) continue;
        const auto got = ::read(reader.fd, buffer, sizeof(buffer));
        if (got > 0) { result.output.append(buffer, static_cast<std::size_t>(got)); continue; }
        if (got < 0 && errno == EINTR) continue;
        break;   // EOF or error
    }
    if (result.isTimedOut) ::kill(child, SIGKILL);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    if (!result.isTimedOut && WIFEXITED(status)) result.exitCode = WEXITSTATUS(status);
    return result;
}
}
