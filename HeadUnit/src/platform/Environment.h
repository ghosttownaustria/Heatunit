#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace headunit {
std::optional<std::string> GetEnv(const char* name);
std::filesystem::path UserStateDirectory();
}
