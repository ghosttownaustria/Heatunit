#pragma once

// The groups of ProtocolTests; each runs its tests and throws on the first failure (see Check).
void RunTlsTests();
void RunSessionTests();
void RunInputAudioTests();
#ifdef HEADUNIT_WIRELESS_TESTS
void RunWirelessTests();
#endif
