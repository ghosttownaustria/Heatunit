#pragma once
#include "androidauto/ProjectionInput.h"
#include "audio/AudioTypes.h"
#include "logging/Logger.h"
#include "video/VideoDecoder.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
namespace aasdk::transport { class ITransport; }
namespace headunit {
struct ProjectionCallbacks {
    std::function<void(const std::string&)> onStatus;
    std::function<void(VideoFrame)> onFrame;
    // Touch, keys and rotary input from the window; the session attaches to it while it runs.
    std::shared_ptr<ProjectionInput> input;
    // Opens the speaker for one of the phone's audio streams. Without it audio is acknowledged
    // and dropped, which keeps the phone's session alive.
    AudioOpener openAudio;
};
struct ProjectionResult {
    bool hasVideo{};
    // The phone answered the version request. A session that ends without it never reached
    // Android Auto on the phone, which callers may treat as "try the connection again".
    bool hasVersionReply{};
    bool isStoppedByUser{};
    std::string message;
};
// Runs on a worker. The caller retains the USB interface until this returns.
ProjectionResult RunAndroidAutoSession(std::shared_ptr<aasdk::transport::ITransport> transport,
    Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
