// The one translation unit that compiles miniaudio (third_party/miniaudio, a single-header library).
// The build compiles it without warnings: it is third-party code.
#include "audio/MiniaudioConfig.h"
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
