#include "audio/WatchedOutput.h"
#include "audio/AudioClock.h"
#include <utility>

namespace headunit {
// Wraps `output`; `activity` hears of every block written to it.
WatchedOutput::WatchedOutput(std::shared_ptr<IPcmOutput> output, std::shared_ptr<MediaActivity> activity)
    : m_output(std::move(output)), m_activity(std::move(activity))
{
}

// Notes the block (when it holds audio) and passes it on.
void WatchedOutput::Write(std::span<const std::uint8_t> pcm)
{
    if (!pcm.empty()) m_activity->NoteAudio(SteadyNowMs());
    m_output->Write(pcm);
}

// Passes the flush on.
void WatchedOutput::Flush()
{
    m_output->Flush();
}

// What the wrapped output has queued.
std::size_t WatchedOutput::Queued() const
{
    return m_output->Queued();
}
}
