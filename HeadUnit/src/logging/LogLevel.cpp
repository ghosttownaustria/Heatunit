#include "logging/LogLevel.h"
#include "platform/Environment.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace headunit {
// The level as it appears in a log line ("[INFO]").
std::string_view LogLevelText(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

// Reads a level name as HEADUNIT_LOG_LEVEL gives it ("trace", "debug", "info", "warning" or "warn", "error"), in any case.
std::optional<LogLevel> ParseLogLevel(std::string_view text)
{
    std::string name(text);
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (name == "trace") return LogLevel::Trace;
    if (name == "debug") return LogLevel::Debug;
    if (name == "info") return LogLevel::Info;
    if (name == "warning" || name == "warn") return LogLevel::Warning;
    if (name == "error") return LogLevel::Error;
    return std::nullopt;
}

// The minimum level the build profile asks for: everything for the *_level_log profiles, the debug lines for a
// debug build, and what a user reads for a release build.
LogLevel BuildDefaultLogLevel()
{
#if defined(HEADUNIT_VERBOSE_LOGGING)
    return LogLevel::Trace;
#elif defined(NDEBUG)
    return LogLevel::Info;
#else
    return LogLevel::Debug;
#endif
}

// The minimum level of this run: HEADUNIT_LOG_LEVEL when it names a level, otherwise the build profile's default.
LogLevel ConfiguredLogLevel()
{
    if (const auto text = GetEnv("HEADUNIT_LOG_LEVEL")) {
        if (const auto level = ParseLogLevel(*text)) return *level;
    }
    return BuildDefaultLogLevel();
}
}
