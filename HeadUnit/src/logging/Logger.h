#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace headunit {
// Where the log file `name` goes: the working directory when it is writable (as always), otherwise the
// per-user state directory ($XDG_STATE_HOME/headunit or ~/.local/state/headunit, %LOCALAPPDATA%\HeadUnit).
// A launcher that starts the program in a read-only directory then still gets a log, not a startup failure.
std::filesystem::path DefaultLogPath(const std::string& name);

class Logger {
public:
    explicit Logger(const std::filesystem::path& path);
    void Write(std::string_view level, std::string_view component, std::string_view message);
private:
    std::mutex m_mutex;
    std::ofstream m_file;
};
}
