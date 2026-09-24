#pragma once
#include "audio/AudioTypes.h"
#include "logging/Logger.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace headunit {
// The headunit's own player: a music file or an internet radio stream, decoded by FFmpeg (any container and codec it
// knows, http and https included) and played as media audio through the same output as the phone's music, so volume,
// mute and the level display apply to it too. One source at a time; it plays on its own thread and never blocks the
// caller. Paced by the output's queue, so a file plays in real time.
class AudioPlayer {
public:
    enum class State { Stopped, Opening, Playing, Paused, Ended, Failed };
    struct Status {
        State state{State::Stopped};
        std::string source;         // what Play was given
        unsigned generation{};      // counts Play calls: tells one run of a source from the next
        std::string title, artist;  // from the file's tags (empty when it has none)
        std::string streamTitle;    // what a radio station says is playing now
        double position{};          // seconds played
        double duration{};          // seconds; 0 for a live stream
        std::string error;          // why it failed, for the user
    };
    AudioPlayer(AudioOpener opener, Logger& logger);
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;
    // Starts `source` (a file path in UTF-8 or an http(s) URL), ending whatever played before.
    void Play(const std::string& source);
    // Pausing holds a file where it is; a live stream should rather be stopped.
    void SetPaused(bool isPaused);
    void Stop();
    Status CurrentStatus() const;
    bool IsActive() const;   // opening, playing or paused
private:
    struct Run;
    void Worker();
    void Decode(Run& run);
    bool WaitForRoom(Run& run, IPcmOutput& output);
    void Update(const Run& run, const std::function<void(Status&)>& change);
    std::shared_ptr<IPcmOutput> Output();
    AudioOpener m_opener;
    Logger& m_logger;
    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    Status m_status;                        // a run only changes it while it is the newest one
    std::shared_ptr<Run> m_current;         // being played by the worker
    std::shared_ptr<Run> m_pending;         // asked for, the worker starts it once the current one has let go
    std::shared_ptr<IPcmOutput> m_output;   // opened on the first run, kept for the next ones
    bool m_isQuitting{};
    std::thread m_thread;                   // one worker for the player's lifetime: a slow connect never blocks the caller
};
}
