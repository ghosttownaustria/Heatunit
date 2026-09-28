#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "CommandLine.h"
#include <string>
#include <string_view>
#include <vector>

using namespace headunit;

namespace {
// The command line of `arguments` for a build with wireless Android Auto.
CommandLine Parse(std::vector<std::string_view> arguments, bool hasWireless = true)
{
    return ParseCommandLine(arguments, hasWireless);
}

// Without arguments the window runs; a mode option and a display size are taken over.
void TestAcceptedArguments()
{
    const auto plain = Parse({});
    Check(plain.mode == RunMode::Window && !plain.display && !plain.isHelpRequested && !plain.isWirelessRequested && plain.error.empty(),
        "No arguments did not start the window");
    const auto scan = Parse({"--scan"});
    Check(scan.mode == RunMode::Scan && scan.error.empty(), "--scan was not understood");
    const auto smoke = Parse({"--display", "1280x720", "--smoke-test"});
    Check(smoke.mode == RunMode::SmokeTest && smoke.display == DisplayConfig{1280, 720} && smoke.error.empty(), "A display and a mode were not both taken");
    const auto wireless = Parse({"--wireless"});
    Check(wireless.mode == RunMode::Window && wireless.isWirelessRequested && wireless.error.empty(), "--wireless was not accepted");
    Check(Parse({"--test-bluetooth"}).mode == RunMode::TestBluetooth && Parse({"--test-hotspot"}).mode == RunMode::TestHotspot, "The wireless tests were not understood");
}

// --help ends the reading, also before an argument that would be refused.
void TestHelp()
{
    const auto help = Parse({"--help", "--bogus"});
    Check(help.isHelpRequested && help.error.empty(), "--help did not end the reading");
    Check(CommandLineHelp(true).find("--test-bluetooth") != std::string::npos, "The help does not name the wireless tests");
    Check(CommandLineHelp(false).find("--test-bluetooth") == std::string::npos, "The help names wireless tests a build without wireless lacks");
    Check(CommandLineHelp(false).find("HEADUNIT_LOG_LEVEL") != std::string::npos, "The help does not name the log level override");
}

// Unknown options, two modes, a missing or unknown display size and the wireless options of a build without them are refused.
void TestRefusedArguments()
{
    Check(Parse({"--bogus"}).error == "Unknown option: --bogus", "An unknown option was accepted");
    Check(!Parse({"--scan", "--smoke-test"}).error.empty(), "Two run modes were accepted");
    Check(!Parse({"--wireless", "--scan"}).error.empty(), "--wireless and a run mode were accepted together");
    Check(Parse({"--display"}).error.find("1280x720") != std::string::npos, "A missing display size did not list the sizes");
    Check(!Parse({"--display", "1024x600"}).error.empty(), "An unknown display size was accepted");
    Check(!Parse({"--test-bluetooth"}, false).error.empty() && !Parse({"--wireless"}, false).error.empty(),
        "A build without wireless accepted a wireless option");
}
}

// The program's command line.
void RunCommandLineTests()
{
    TestAcceptedArguments();
    TestHelp();
    TestRefusedArguments();
}
