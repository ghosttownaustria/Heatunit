#include "logging/Logger.h"
#include "platform/Environment.h"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace headunit {
std::filesystem::path DefaultLogPath(const std::string& name)
{
    std::error_code error;
    const auto local = std::filesystem::current_path(error) / name;
    if (!error && std::ofstream(local, std::ios::app)) return local;
    std::filesystem::path directory;
#ifdef _WIN32
    if (const auto base = GetEnv("LOCALAPPDATA"); base && !base->empty()) directory = std::filesystem::path(*base) / "HeadUnit";
#else
    if (const auto state = GetEnv("XDG_STATE_HOME"); state && !state->empty()) directory = std::filesystem::path(*state) / "headunit";
    else if (const auto home = GetEnv("HOME"); home && !home->empty()) directory = std::filesystem::path(*home) / ".local" / "state" / "headunit";
#endif
    if (directory.empty()) return local;
    std::filesystem::create_directories(directory, error);
    return directory / name;
}
Logger::Logger(const std::filesystem::path& path) : m_file(path, std::ios::app)
{
    if (!m_file) throw std::runtime_error("Cannot open log file " + path.string());
}
void Logger::Write(std::string_view level, std::string_view component, std::string_view message)
{
    const std::lock_guard lock(m_mutex);
    const auto now = std::chrono::system_clock::now();
    const auto day = std::chrono::floor<std::chrono::days>(now);
    const std::chrono::year_month_day date(day);
    const std::chrono::hh_mm_ss clock(std::chrono::floor<std::chrono::milliseconds>(now) - day);
    std::string safeMessage(message);
    for (auto& character : safeMessage)
        if (static_cast<unsigned char>(character) < 32) character = ' ';
    std::ostringstream line;
    line << std::setfill('0') << static_cast<int>(date.year()) << '-' << std::setw(2) << static_cast<unsigned>(date.month())
         << '-' << std::setw(2) << static_cast<unsigned>(date.day()) << ' ' << std::setw(2) << clock.hours().count()
         << ':' << std::setw(2) << clock.minutes().count() << ':' << std::setw(2) << clock.seconds().count()
         << '.' << std::setw(3) << clock.subseconds().count() << " UTC"
         << " [" << level << "][" << component << "] " << safeMessage << '\n';
    std::cout << line.str() << std::flush;
    m_file << line.str() << std::flush;
}
}
