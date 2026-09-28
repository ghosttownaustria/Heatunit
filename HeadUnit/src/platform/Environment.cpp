#include "platform/Environment.h"
#include <cstdlib>

namespace headunit {
// The value of an environment variable, or nothing when it is not set. MSVC deprecates getenv, everything else has
// no reason to avoid it.
std::optional<std::string> GetEnv(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || !value) return std::nullopt;
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    if (!value) return std::nullopt;
    return std::string(value);
#endif
}

// The per-user directory for files the program keeps (%LOCALAPPDATA%\HeadUnit, $XDG_STATE_HOME/headunit or
// ~/.local/state/headunit); empty when the environment names none.
std::filesystem::path UserStateDirectory()
{
#ifdef _WIN32
    if (const auto base = GetEnv("LOCALAPPDATA"); base && !base->empty()) return std::filesystem::path(*base) / "HeadUnit";
#else
    if (const auto state = GetEnv("XDG_STATE_HOME"); state && !state->empty()) return std::filesystem::path(*state) / "headunit";
    if (const auto home = GetEnv("HOME"); home && !home->empty()) return std::filesystem::path(*home) / ".local" / "state" / "headunit";
#endif
    return {};
}
}
