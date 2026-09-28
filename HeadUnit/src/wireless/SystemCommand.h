#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace headunit {
// How a program that RunCommand ran ended.
struct CommandResult {
    bool isStarted{};      // the program could be run at all (false: not installed)
    bool isTimedOut{};
    int exitCode{-1};
    std::string output;    // standard output and standard error together
};

CommandResult RunCommand(const std::vector<std::string>& arguments, std::chrono::milliseconds timeout);
}
