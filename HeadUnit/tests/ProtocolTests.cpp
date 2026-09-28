#include "ProtocolTestSuites.h"
#include <exception>
#include <iostream>

// Runs every group of the protocol tests; the first failure ends the run with exit code 1.
int main()
{
    try {
        RunTlsTests();
        RunSessionTests();
        RunInputAudioTests();
#ifdef HEADUNIT_WIRELESS_TESTS
        RunWirelessTests();
#endif
        std::cout << "All protocol tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
