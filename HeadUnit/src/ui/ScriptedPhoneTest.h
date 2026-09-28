#pragma once
#include "ui/ScriptedTestHost.h"
#include <QElapsedTimer>
#include <string>
#include <vector>

namespace headunit {
// One of the scripted checks against a real phone (--test-input, --test-audio, --test-console, --test-keys). It advances
// step by step on the window's tick while the connection runs and reports its result through the host, which then ends
// the connection and exits with the result.
class ScriptedPhoneTest {
public:
    // Which check runs.
    enum class Kind { Input, Audio, Console, Keys };

    ScriptedPhoneTest(Kind kind, ScriptedTestHost& host);

    void Tick();

private:
    Kind m_kind;
    ScriptedTestHost& m_host;
    QElapsedTimer m_clock;
    int m_stage{};
    unsigned m_frames[4]{};
    unsigned m_playRequests{};
    std::string m_problems;
    std::vector<std::string> m_steps;   // --test-keys script

    qint64 Elapsed() const;
    void NextStage(int stage);
    bool IsPictureReady() const;
    void RunInputTest();
    void RunAudioTest();
    void RunConsoleTest();
    void RunKeysTest();
    void PlayKeysStep(const std::string& step);
    void ExpectScreen(ConsoleController::Screen screen, const char* problem);
    void ExpectPhone(PhoneScreen phone, const char* problem);
    void ExpectFront(ScriptedTestHost::FrontView view, const char* problem);
};
}
