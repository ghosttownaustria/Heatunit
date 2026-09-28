#pragma once
#include "androidauto/InputEvents.h"
#include <cstdint>
#include <functional>
#include <mutex>

namespace headunit {
// Carries input from the window (GUI thread) into a running Android Auto session (protocol thread). Events sent while
// no session is attached are dropped: the phone would ignore them anyway, and a stale touch must never reach the next
// session. The window's functions may be called from any thread.
class ProjectionInput {
public:
    using Sink = std::function<void(const InputEvent&)>;

    void Touch(TouchAction action, int x, int y, unsigned pointerId = 0);
    void Key(unsigned keycode, bool isDown);
    void Tap(unsigned keycode);
    void Rotate(int delta);
    bool IsAttached() const;
    std::uint64_t Attach(Sink sink);
    void Detach(std::uint64_t token);

private:
    mutable std::mutex m_mutex;
    Sink m_sink;
    std::uint64_t m_token{};

    void Send(const InputEvent& event);
};
}
