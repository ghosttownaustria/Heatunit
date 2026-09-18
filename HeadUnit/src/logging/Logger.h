#pragma once
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace headunit {
class Logger {
public:
    explicit Logger(const std::filesystem::path& path);
    void Write(std::string_view level, std::string_view component, std::string_view message);
private:
    std::mutex m_mutex;
    std::ofstream m_file;
};
}
