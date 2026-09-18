#include "logging/Logger.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace headunit {
Logger::Logger(const std::filesystem::path& path) : m_file(path, std::ios::app)
{
    if (!m_file) throw std::runtime_error("Cannot open log file");
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
