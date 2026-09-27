#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace headunit {
struct CommandResult {
    bool isStarted{};      // the program could be run at all (false: not installed)
    bool isTimedOut{};
    int exitCode{-1};
    std::string output;    // standard output and standard error together
};
// Runs a program without a shell, so nothing in the arguments (an SSID, a password) is ever interpreted. The
// output is untranslated (LC_ALL=C). A program that runs longer than `timeout` is killed.
CommandResult RunCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout);
}
