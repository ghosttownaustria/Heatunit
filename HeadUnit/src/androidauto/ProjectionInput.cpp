#include "androidauto/ProjectionInput.h"
#include <utility>

namespace headunit {
// A touch point, in touchscreen coordinates of the display announced to the phone.
void ProjectionInput::Touch(TouchAction action, int x, int y, unsigned pointerId)
{
    Send(TouchInput{action, x, y, pointerId});
}

// A key press (`isDown`) or release.
void ProjectionInput::Key(unsigned keycode, bool isDown)
{
    Send(KeyInput{keycode, isDown});
}

// A press and release of `keycode`.
void ProjectionInput::Tap(unsigned keycode)
{
    Key(keycode, true);
    Key(keycode, false);
}

// A turn of the rotary controller by `delta` detents; a zero turn is not sent.
void ProjectionInput::Rotate(int delta)
{
    if (delta != 0) Send(RotaryInput{delta});
}

// Whether a session takes the events right now.
bool ProjectionInput::IsAttached() const
{
    std::lock_guard lock(m_mutex);
    return static_cast<bool>(m_sink);
}

// Session side: from now on every event goes to `sink`. The sink runs under the lock, so once Detach returns it is
// never called again; it must only hand the event over (post), never block. Returns a token; only the holder of the
// current token can detach, so that a finished session can never detach its successor.
std::uint64_t ProjectionInput::Attach(Sink sink)
{
    std::lock_guard lock(m_mutex);
    m_sink = std::move(sink);
    return ++m_token;
}

// Session side: stops the events, unless another session has attached since `token` was handed out.
void ProjectionInput::Detach(std::uint64_t token)
{
    std::lock_guard lock(m_mutex);
    if (token == m_token) m_sink = nullptr;
}

// Hands one event to the attached session, if any.
void ProjectionInput::Send(const InputEvent& event)
{
    std::lock_guard lock(m_mutex);
    if (m_sink) m_sink(event);
}
}
