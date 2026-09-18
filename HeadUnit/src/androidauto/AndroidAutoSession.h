#pragma once
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
};
struct ProjectionResult { bool hasVideo{}; std::string message; };
// Runs on a worker. The caller retains the USB interface until this returns.
ProjectionResult RunAndroidAutoSession(std::shared_ptr<aasdk::transport::ITransport> transport,
    Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
}
