#include "TestSupport.h"
#include <filesystem>
#include <stdexcept>

// Fails the running test with `message` unless `isValid`.
void Check(bool isValid, const std::string& message)
{
    if (!isValid) throw std::runtime_error(message);
}

// The log the code under test writes to, in the temporary directory.
headunit::Logger& TestLogger()
{
    static headunit::Logger logger(std::filesystem::temp_directory_path() / "headunit-tests.log");
    return logger;
}
