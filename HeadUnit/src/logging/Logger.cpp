#include "logging/Logger.h"
#include "platform/Environment.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace headunit {
namespace {
// "2026-09-28 17:16:52.591 UTC": the time of a log line.
std::string TimestampText()
{
    const auto now = std::chrono::system_clock::now();
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day date(day);
    const std::chrono::hh_mm_ss clock(std::chrono::floor<std::chrono::milliseconds>(now) - day);
    std::ostringstream text;
    text << std::setfill('0') << static_cast<int>(date.year()) << '-' << std::setw(2) << static_cast<unsigned>(date.month())
         << '-' << std::setw(2) << static_cast<unsigned>(date.day()) << ' ' << std::setw(2) << clock.hours().count()
         << ':' << std::setw(2) << clock.minutes().count() << ':' << std::setw(2) << clock.seconds().count()
         << '.' << std::setw(3) << clock.subseconds().count() << " UTC";
    return text.str();
}
}

// Opens (appends to) the log file at `path`; throws when it cannot be written.
Logger::Logger(const std::filesystem::path& path, LogLevel minimumLevel) : m_file(path, std::ios::app), m_minimumLevel(minimumLevel)
{
    if (!m_file) throw std::runtime_error("Cannot open log file " + path.string());
}

// Writes one line when `level` is enabled. Control characters in the message become spaces, so one call is one line.
void Logger::Write(LogLevel level, std::string_view tag, std::string_view message)
{
    if (!IsEnabled(level)) return;
    std::string safeMessage(message);
    for (auto& character : safeMessage) {
        if (static_cast<unsigned char>(character) < 32) character = ' ';
    }
    // The time is taken under the lock, so the lines of all threads stay in time order.
    const std::lock_guard lock(m_mutex);
    std::ostringstream line;
    line << TimestampText() << " [" << LogLevelText(level) << "][" << tag << "] " << safeMessage << '\n';
    std::cout << line.str() << std::flush;
    m_file << line.str() << std::flush;
}

// Whether lines of `level` are written; callers use it to skip building expensive messages.
bool Logger::IsEnabled(LogLevel level) const
{
    return level >= m_minimumLevel;
}

// Where the log file `name` goes: the working directory when it is writable (as always), otherwise the per-user state
// directory. A launcher that starts the program in a read-only directory then still gets a log, not a startup failure.
std::filesystem::path DefaultLogPath(const std::string& name)
{
    std::error_code error;
    const auto local = std::filesystem::current_path(error) / name;
    if (!error && std::ofstream(local, std::ios::app)) return local;
    const auto directory = UserStateDirectory();
    if (directory.empty()) return local;
    std::filesystem::create_directories(directory, error);
    return directory / name;
}
}
