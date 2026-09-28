#include "ProtocolTestSuites.h"
#include "FakeTransports.h"
#include "TestSupport.h"
#include "androidauto/AndroidAutoSession.h"
#include "usb/ProjectionTransport.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>

namespace {
using Data = aasdk::common::Data;

// The whole content of a text file.
std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
// A session whose transport is gone before it starts ends at once, reports no video and stops the transport.
void TestCancelledSession() {
    std::atomic_bool isStopRequested{true};
    auto transport = std::make_shared<StoppedTransport>();
    const auto result = headunit::RunAndroidAutoSession(transport, TestLogger(), isStopRequested, {});
    Check(!result.hasVideo && transport->HasStopped(), "Cancelled session reported video or retained transport");
}

// The user presses "Verbindung beenden" while the phone has not answered yet: the session
// must end promptly, stop its transport and leave no object behind.
void TestStopWhileWaitingForPhone() {
    const auto logPath = std::filesystem::temp_directory_path() / "headunit-protocol-stop-tests.log";
    std::filesystem::remove(logPath);
    headunit::Logger logger(logPath);
    std::atomic_bool isStopRequested{false};
    auto transport = std::make_shared<SilentTransport>();
    std::thread user([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); isStopRequested = true; });
    const auto begin = std::chrono::steady_clock::now();
    const auto result = headunit::RunAndroidAutoSession(transport, logger, isStopRequested, {});
    user.join();
    Check(std::chrono::steady_clock::now() - begin < std::chrono::seconds(5), "Stopping a waiting session took too long");
    Check(transport->HasStopped() && !result.hasVideo, "Stopped session kept its transport or reported video");
    Check(result.message.find("stopped by user") != std::string::npos, "Stop reason was not reported");
    Check(ReadFile(logPath).find("ownership cycle") == std::string::npos, "Session leaked after being stopped");
}
// stop() must be repeatable and must reject later requests instead of dropping them,
// otherwise a protocol layer waits forever for a completion that never comes.
void TestTransportStop() {
    boost::asio::io_context io;
    auto transport = std::make_shared<headunit::ProjectionTransport>(nullptr, std::uint8_t{0x81}, std::uint8_t{0x01});
    transport->stop();
    transport->stop();
    int rejected = 0;
    auto receive = aasdk::transport::ITransport::ReceivePromise::defer(io);
    receive->then([](Data) {}, [&](const aasdk::error::Error& error) { rejected += error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED; });
    transport->receive(16, receive);
    auto send = aasdk::transport::ITransport::SendPromise::defer(io);
    send->then([] {}, [&](const aasdk::error::Error& error) { rejected += error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED; });
    transport->send(Data(4), send);
    io.run();
    Check(rejected == 2, "Requests after stop() were not rejected");
}
}

// Starting and stopping sessions and transports.
void RunSessionTests()
{
    TestCancelledSession();
    TestStopWhileWaitingForPhone();
    TestTransportStop();
}
