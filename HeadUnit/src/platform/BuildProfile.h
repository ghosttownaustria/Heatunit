#pragma once

namespace headunit {
// Whether this is a build for the car (the release profiles): the head unit then takes the whole screen and has none of
// the simulated console and controls of the development window (see MainWindow::EnterKioskMode).
#ifdef HEADUNIT_KIOSK
inline constexpr bool kIsKioskBuild = true;
#else
inline constexpr bool kIsKioskBuild = false;
#endif
}
