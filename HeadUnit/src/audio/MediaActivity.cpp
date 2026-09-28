#include "audio/MediaActivity.h"

namespace headunit {
// A block of the phone's media audio arrived (protocol thread); after a gap it counts as a new start.
void MediaActivity::NoteAudio(std::int64_t nowMs)
{
    if (nowMs - m_lastMs.load() > kGapMs) m_startMs = nowMs;
    m_lastMs = nowMs;
}

// When the phone's media audio last started after a pause.
std::int64_t MediaActivity::StartMs() const
{
    return m_startMs;
}

// Whether the phone's media audio is running at `nowMs`.
bool MediaActivity::IsSounding(std::int64_t nowMs) const
{
    return nowMs - m_lastMs.load() <= kGapMs;
}
}
