#pragma once
#include "androidauto/DisplayConfig.h"

// The display sizes the tests run through: the common ones, a screen that no frame fits exactly (1024x600, taller than its
// frame), a very wide one and one larger than the largest frame.
inline constexpr headunit::DisplayConfig kTestDisplays[] = {{800, 480}, {1024, 600}, {1280, 720}, {1600, 600}, {1920, 1080}, {2560, 1440}};
