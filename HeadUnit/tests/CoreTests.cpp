#include "CoreTestSuites.h"
#include <exception>
#include <iostream>

// Runs every group of the portable core's tests; the first failure ends the run with exit code 1.
int main()
{
    try {
        RunUsbDescriptorTests();
        RunAoaTests();
        RunAutoConnectTests();
        RunPhoneWatchTests();
        RunDisplayTests();
        RunConsoleTests();
        RunAudioTests();
        RunMenuTests();
        RunMediaTests();
        RunLoggingTests();
        RunCommandLineTests();
        RunTransportBufferTests();
        std::cout << "All core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
