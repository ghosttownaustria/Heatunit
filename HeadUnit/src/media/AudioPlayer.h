#pragma once
#include "audio/IPcmOutput.h"
#include "logging/Logger.h"
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>

namespace headunit {
// The head unit's own player: a music file or an internet radio stream, decoded by FFmpeg (any container and codec it
// knows, http and https included) and played as media audio through the same output as the phone's music, so volume,
// mute and the level display apply to it too. One source at a time; it plays on its own thread and never blocks the
// caller. Paced by the output's queue, so a file plays in real time.
class AudioPlayer {
public:
    AudioPlayer(AudioOpener opener, Logger& logger);
    ~AudioPlayer();
    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    // What the player does.
    enum class State { Stopped, Opening, Playing, Paused, Ended, Failed };

    // What the pages show.
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

    void Play(const std::string& source);
    void SetPaused(bool isPaused);
    void Stop();
    Status CurrentStatus() const;
    bool IsActive() const;

private:
    struct Run;
    struct Decoding;

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

    void Worker();
    void Decode(Run& run);
    bool OpenSource(Run& run, bool isStream, Decoding& decoding);
    void PlayDecoded(Run& run, bool isStream, double duration, Decoding& decoding, IPcmOutput& output);
    bool WriteBlock(Run& run, IPcmOutput& output, std::span<const std::uint8_t> block, std::uint64_t& writtenBytes);
    void UpdateProgress(const Run& run, bool isStream, Decoding& decoding, std::uint64_t writtenBytes, const IPcmOutput& output);
    bool WaitForRoom(Run& run, IPcmOutput& output);
    void Update(const Run& run, const std::function<void(Status&)>& change);
    void Fail(const Run& run, const std::string& message, int code);
    std::shared_ptr<IPcmOutput> Output();
};
}
