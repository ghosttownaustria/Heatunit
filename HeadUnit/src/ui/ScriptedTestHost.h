#pragma once
#include "androidauto/ConsoleController.h"
#include "androidauto/PhoneScreenDetector.h"
#include "androidauto/ProjectionInput.h"
#include "audio/AudioState.h"
#include "logging/Logger.h"
#include <string>

namespace headunit {
// The frames the phone must have drawn before a scripted test (or the projection test) takes it as running.
inline constexpr unsigned kProjectionTestFrames = 10;

// What a scripted phone test (ScriptedPhoneTest) needs from the window: the phone's input and picture, the audio state,
// the console, the window's own pages and log, and a way to report the result.
class ScriptedTestHost {
public:
    virtual ~ScriptedTestHost() = default;

    // Which of the window's views is in front.
    enum class FrontView { PhonePicture, HomeMenu, Tuner, Other };

    // How many frames of the phone the window has shown so far in this connection.
    virtual unsigned DisplayedFrames() const = 0;
    // The input bus to the phone.
    virtual ProjectionInput& PhoneInput() = 0;
    // The car's audio state (volume, bytes played).
    virtual AudioState& Audio() = 0;
    // The display announced to the phone.
    virtual DisplayConfig AnnouncedDisplay() const = 0;
    // What the phone shows, read from its last picture.
    virtual PhoneScreen CurrentPhoneScreen() const = 0;
    // Where the console thinks the car is.
    virtual ConsoleController::Screen ConsoleScreen() const = 0;
    // Which view is in front.
    virtual FrontView FrontViewShown() const = 0;
    // Whether the window's log shows a line containing `text`.
    virtual bool HasLogLine(const char* text) const = 0;
    // Presses a controller key as the console does.
    virtual void PressConsole(ConsoleKey key) = 0;
    // Saves a picture of the phone (or the whole window) for a person to look at.
    virtual void SaveTestShot(const char* name, bool isWholeWindow = false) = 0;
    // Ends the test with its result; the connection ends and the program exits with `exitCode`.
    virtual void FinishTest(int exitCode, const std::string& summary) = 0;
    // The program's log.
    virtual Logger& TestLog() = 0;
};
}
