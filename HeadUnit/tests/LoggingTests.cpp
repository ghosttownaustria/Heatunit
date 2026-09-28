#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "logging/LogLevel.h"
#include "logging/Logger.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

using namespace headunit;

namespace {
// The whole content of a text file.
std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// The level names of HEADUNIT_LOG_LEVEL are read in any case; anything else is no level.
void TestLevelNames()
{
    Check(ParseLogLevel("trace") == LogLevel::Trace && ParseLogLevel("DEBUG") == LogLevel::Debug && ParseLogLevel("Info") == LogLevel::Info,
        "A level name was not read");
    Check(ParseLogLevel("warning") == LogLevel::Warning && ParseLogLevel("warn") == LogLevel::Warning && ParseLogLevel("error") == LogLevel::Error,
        "The warning or error level was not read");
    Check(!ParseLogLevel("") && !ParseLogLevel("verbose") && !ParseLogLevel(" info"), "An unknown level name was accepted");
    Check(LogLevelText(LogLevel::Warning) == "WARN" && LogLevelText(LogLevel::Trace) == "TRACE", "A level is written wrongly");
    for (const auto level : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warning, LogLevel::Error})
        Check(ParseLogLevel(LogLevelText(level)) == level, "A written level does not read back");
}

// The logger leaves out the lines below its minimum level and marks the others with level and tag.
void TestMinimumLevel()
{
    const auto path = std::filesystem::temp_directory_path() / "headunit-logging-tests.log";
    std::filesystem::remove(path);
    {
        Logger logger(path, LogLevel::Warning);
        Check(!logger.IsEnabled(LogLevel::Info) && logger.IsEnabled(LogLevel::Warning) && logger.IsEnabled(LogLevel::Error),
            "The logger reports the wrong levels as enabled");
        logger.Write(LogLevel::Debug, "TEST", "left out: debug");
        logger.Write(LogLevel::Info, "TEST", "left out: info");
        logger.Write(LogLevel::Warning, "TEST", "kept: warning");
        logger.Write(LogLevel::Error, "TEST", "kept: error");
    }
    const auto text = ReadFile(path);
    Check(text.find("left out") == std::string::npos, "A line below the minimum level was written");
    Check(text.find("[WARN][TEST] kept: warning") != std::string::npos && text.find("[ERROR][TEST] kept: error") != std::string::npos,
        "A line at or above the minimum level is missing or badly marked");
}
}

// The log levels and the logger's filtering.
void RunLoggingTests()
{
    TestLevelNames();
    TestMinimumLevel();
}
