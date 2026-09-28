#pragma once
#include <optional>
#include <string_view>

namespace headunit {
// How important a log line is. The logger writes the lines at or above its minimum level: Trace adds the Android Auto
// protocol trace, Debug the USB descriptor details, Info is what a user reads.
enum class LogLevel { Trace, Debug, Info, Warning, Error };

std::string_view LogLevelText(LogLevel level);
std::optional<LogLevel> ParseLogLevel(std::string_view text);
LogLevel BuildDefaultLogLevel();
LogLevel ConfiguredLogLevel();
}
