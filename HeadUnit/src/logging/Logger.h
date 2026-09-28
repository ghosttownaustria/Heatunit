#pragma once
#include "logging/LogLevel.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace headunit {
// Writes timestamped lines ("<time> UTC [LEVEL][TAG] message") to a log file and to standard output, leaving out the
// lines below its minimum level. Safe to use from any thread.
class Logger {
public:
    explicit Logger(const std::filesystem::path& path, LogLevel minimumLevel = ConfiguredLogLevel());

    void Write(LogLevel level, std::string_view tag, std::string_view message);
    bool IsEnabled(LogLevel level) const;

private:
    std::mutex m_mutex;
    std::ofstream m_file;
    LogLevel m_minimumLevel;
};

std::filesystem::path DefaultLogPath(const std::string& name);
}
