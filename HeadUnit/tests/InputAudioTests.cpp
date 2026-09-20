#include "androidauto/AndroidAutoSession.h"
#include "androidauto/DisplayService.h"
#include "androidauto/InputReports.h"
#include <aasdk/Transport/ITransport.hpp>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>
#include <utility>

using namespace headunit;
namespace input = aap_protobuf::service::inputsource::message;

namespace {
void Require(bool isValid, const char* message) { if (!isValid) throw std::runtime_error(message); }

// A phone that connected but never says anything: the read stays pending until stop().
class QuietTransport final : public aasdk::transport::ITransport {
public:
    void receive(std::size_t, ReceivePromise::Pointer promise) override { m_pending = std::move(promise); }
    void send(aasdk::common::Data, SendPromise::Pointer promise) override { promise->resolve(); }
    void stop() override {
        if (m_pending) std::exchange(m_pending, nullptr)->reject(aasdk::error::Error(aasdk::error::ErrorCode::OPERATION_ABORTED));
    }
    ReceivePromise::Pointer m_pending;
};

void TestInputReports() {
    const auto down = BuildInputReport(TouchInput{TouchAction::Down, 123, 456, 0}, 42);
    Require(down.timestamp() == 42 && down.has_touch_event() && !down.has_key_event() && !down.has_relative_event(), "Touch report has the wrong shape");
    Require(down.touch_event().action() == input::ACTION_DOWN && down.touch_event().pointer_data_size() == 1, "Touch down action is wrong");
    Require(down.touch_event().pointer_data(0).x() == 123 && down.touch_event().pointer_data(0).y() == 456 && down.touch_event().pointer_data(0).pointer_id() == 0,
        "Touch coordinates changed");
    Require(BuildInputReport(TouchInput{TouchAction::Move, 1, 2, 0}, 1).touch_event().action() == input::ACTION_MOVED, "Touch move action is wrong");
    Require(BuildInputReport(TouchInput{TouchAction::Up, 1, 2, 0}, 1).touch_event().action() == input::ACTION_UP, "Touch up action is wrong");

    const auto key = BuildInputReport(KeyInput{keys::Home, true}, 7);
    Require(key.has_key_event() && !key.has_touch_event() && key.key_event().keys_size() == 1, "Key report has the wrong shape");
    Require(key.key_event().keys(0).keycode() == keys::Home && key.key_event().keys(0).down() && key.key_event().keys(0).metastate() == 0, "Key press is wrong");
    Require(!BuildInputReport(KeyInput{keys::Home, false}, 7).key_event().keys(0).down(), "Key release is wrong");

    const auto turn = BuildInputReport(RotaryInput{-3}, 9);
    Require(turn.has_relative_event() && turn.relative_event().data_size() == 1, "Rotary report has the wrong shape");
    Require(turn.relative_event().data(0).keycode() == keys::RotaryController && turn.relative_event().data(0).delta() == -3, "Rotary turn is wrong");

    // Everything the report carries must survive the wire.
    input::InputReport parsed;
    Require(parsed.ParseFromString(down.SerializeAsString()) && parsed.touch_event().pointer_data(0).x() == 123, "Touch report did not survive serialization");
}

// The display size reaches the phone as the video service's resolution and density.
void TestDisplayService() {
    namespace sink = aap_protobuf::service::media::sink::message;
    const auto standard = BuildVideoConfiguration({800, 480});
    Require(standard.codec_resolution() == sink::VIDEO_800x480 && standard.density() == 160 && standard.frame_rate() == sink::VIDEO_FPS_30 &&
        standard.width_margin() == 0 && standard.height_margin() == 0, "The 800x480 video configuration is wrong");
    const auto hd = BuildVideoConfiguration({1280, 720});
    Require(hd.codec_resolution() == sink::VIDEO_1280x720 && hd.density() == 240, "The 1280x720 video configuration is wrong");
    const auto fullHd = BuildVideoConfiguration({1920, 1080});
    Require(fullHd.codec_resolution() == sink::VIDEO_1920x1080 && fullHd.density() == 360 && fullHd.width_margin() == 0 && fullHd.height_margin() == 0,
        "The 1920x1080 video configuration is wrong");
    // The 1600x600 display rides in a 1920x1080 frame with a 360 px height margin; the phone lays its interface out at 240 dpi in the 1920x720 area.
    const auto ultrawide = BuildVideoConfiguration({1600, 600});
    Require(ultrawide.codec_resolution() == sink::VIDEO_1920x1080 && ultrawide.width_margin() == 0 && ultrawide.height_margin() == 360 &&
        ultrawide.density() == 240 && ultrawide.frame_rate() == sink::VIDEO_FPS_30, "The 1600x600 video configuration is wrong");
    for (const auto& display : kDisplays) Require(VideoResolutionOf(display).has_value(), "An offered display has no video resolution");
    Require(!VideoResolutionOf({3840, 2160}) && !VideoResolutionOf({1920, 1200}) && !VideoResolutionOf({0, 0}), "A display that no frame holds got a video resolution");
    Require(VideoResolutionOf({1600, 600}) == std::optional<sink::VideoCodecResolutionType>(sink::VIDEO_1920x1080), "The video resolution of 1600x600 is wrong");
    Require(VideoResolutionOf({1280, 720}) == std::optional<sink::VideoCodecResolutionType>(sink::VIDEO_1280x720), "The video resolution lookup is wrong");
    sink::VideoConfiguration parsed;
    Require(parsed.ParseFromString(hd.SerializeAsString()) && parsed.codec_resolution() == sink::VIDEO_1280x720 && parsed.density() == 240,
        "The video configuration did not survive serialization");
}

// A session attaches to the window's input bus while it runs and detaches when it ends, whatever
// the reason; a leftover attachment would send the next session's events into a dead one.
void TestSessionInputLifecycle() {
    const auto logPath = std::filesystem::temp_directory_path() / "headunit-input-tests.log";
    std::filesystem::remove(logPath);
    Logger logger(logPath);
    auto bus = std::make_shared<ProjectionInput>();
    std::atomic_bool isStopRequested{false};
    ProjectionCallbacks callbacks;
    callbacks.input = bus;
    std::atomic_bool wasAttached{false};
    std::thread observer([&] {
        for (int i = 0; i < 100 && !wasAttached; ++i) { wasAttached = bus->IsAttached(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        // Input sent before the phone opened the channel is dropped by the session, not queued.
        bus->Tap(keys::Home);
        bus->Rotate(1);
        isStopRequested = true;
    });
    const auto result = RunAndroidAutoSession(std::make_shared<QuietTransport>(), logger, isStopRequested, callbacks);
    observer.join();
    Require(wasAttached, "The session never attached to the input bus");
    Require(!bus->IsAttached(), "The session left the input bus attached");
    Require(result.isStoppedByUser && !result.hasVideo, "The stopped session reported the wrong result");
    bus->Tap(keys::Home);  // must be harmless now
}
}

void RunInputAudioTests() {
    TestInputReports();
    TestDisplayService();
    TestSessionInputLifecycle();
}
