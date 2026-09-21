#pragma once
#include <cstdlib>
#include <optional>
#include <string>

namespace headunit {
// The value of an environment variable, or nothing when it is not set. MSVC deprecates getenv, everything
// else has no reason to avoid it.
inline std::optional<std::string> GetEnv(const char* name)
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
}
