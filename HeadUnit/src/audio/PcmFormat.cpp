#include "audio/PcmFormat.h"

namespace headunit {
// The stream's name in log lines and for the sound system ("Media", "Guidance", "System").
const char* AudioKindName(AudioKind kind)
{
    switch (kind) {
    case AudioKind::Media: return "Media";
    case AudioKind::Guidance: return "Guidance";
    case AudioKind::System: return "System";
    }
    return "System";
}
}
