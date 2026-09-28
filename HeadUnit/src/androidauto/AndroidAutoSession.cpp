// Session orchestration adapted from OpenAuto (GPL-3.0-or-later),
// Copyright (C) 2018 f1x.studio (Michal Szwaj).
// Upstream revisions and local changes: third_party/aasdk/PATCHES.md.
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/ProjectionSession.h"
#include "platform/Environment.h"
#include <aasdk/Common/ModernLogger.hpp>
#include <exception>

namespace headunit {
// Runs one Android Auto session over `transport` on the calling thread (a worker) until it ends. The caller retains the
// USB interface (or socket) until this returns. The protocol trace of the AASDK is on at the Trace log level or with
// HEADUNIT_PROTOCOL_TRACE.
ProjectionResult RunAndroidAutoSession(std::shared_ptr<aasdk::transport::ITransport> transport, Logger& logger, std::atomic_bool& isStopRequested,
    ProjectionCallbacks callbacks)
{
    if (logger.IsEnabled(LogLevel::Trace) || GetEnv("HEADUNIT_PROTOCOL_TRACE"))
        aasdk::common::ModernLogger::getInstance().setLevel(aasdk::common::LogLevel::DEBUG);
    boost::asio::io_context io;
    auto session = std::make_shared<ProjectionSession>(io, transport, logger, isStopRequested, std::move(callbacks));
    const std::weak_ptr<ProjectionSession> observer = session;
    try {
        session->Start();
    } catch (const std::exception& error) {
        session->End(std::string("AA session failed: ") + error.what());
    }
    // A throwing handler must not abandon the handlers still queued behind it: end the session, then keep running until
    // every completion has been delivered.
    for (bool isDrained = false; !isDrained;) {
        try {
            io.run();
            isDrained = true;
        } catch (const std::exception& error) {
            session->End(std::string("AA session failed: ") + error.what());
        }
    }
    transport->stop();
    io.restart();
    io.poll();
    auto result = session->Result();
    session.reset();
    // Everything the session owns must be gone before the caller releases the USB interface; a survivor would mean a
    // promise/handler ownership cycle.
    if (!observer.expired()) logger.Write(LogLevel::Warning, "AA", "Session object is still referenced after shutdown (ownership cycle)");
    return result;
}
}
