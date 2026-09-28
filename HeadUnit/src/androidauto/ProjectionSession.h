#pragma once
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/AudioSinkChannel.h"
#include "androidauto/MicrophoneChannel.h"
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/InputSource/IInputSourceServiceEventHandler.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aasdk/Channel/SensorSource/ISensorSourceServiceEventHandler.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Messenger/Messenger.hpp>
#include <aasdk/Transport/ITransport.hpp>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace headunit {
// One Android Auto session over a transport: the version exchange, the TLS handshake, the service discovery, the video,
// sensor and input channels (the audio and microphone channels have their own handlers), and the watchdogs of a 100 ms
// tick. Everything runs on one strand of the caller's io_context. Adapted from OpenAuto's session orchestration
// (GPL-3.0-or-later, Copyright (C) 2018 f1x.studio (Michal Szwaj)); local changes: third_party/aasdk/PATCHES.md.
class ProjectionSession final : public aasdk::channel::control::IControlServiceChannelEventHandler,
                                public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
                                public aasdk::channel::sensorsource::ISensorSourceServiceEventHandler,
                                public aasdk::channel::inputsource::IInputSourceServiceEventHandler,
                                public std::enable_shared_from_this<ProjectionSession> {
public:
    ProjectionSession(boost::asio::io_context& io, std::shared_ptr<aasdk::transport::ITransport> transport, Logger& logger,
        std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks);
    ~ProjectionSession() override;

    void Start();
    void End(const std::string& reason);
    void BeginShutdown();
    ProjectionResult Result() const;
    void HandleInput(const InputEvent& event);

    void onVersionResponse(std::uint16_t major, std::uint16_t minor, aap_protobuf::shared::MessageStatus status) override;
    void onHandshake(const aasdk::common::DataConstBuffer& payload) override;
    void onServiceDiscoveryRequest(const aap_protobuf::service::control::message::ServiceDiscoveryRequest& request) override;
    void onChannelOpenRequest(const aap_protobuf::service::control::message::ChannelOpenRequest& request) override;
    void onMediaChannelSetupRequest(const aap_protobuf::service::media::shared::message::Setup& request) override;
    void onMediaChannelStartIndication(const aap_protobuf::service::media::shared::message::Start& start) override;
    void onMediaChannelStopIndication(const aap_protobuf::service::media::shared::message::Stop& stop) override;
    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType timestamp, const aasdk::common::DataConstBuffer& buffer) override;
    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override;
    void onVideoFocusRequest(const aap_protobuf::service::media::video::message::VideoFocusRequestNotification& request) override;
    void onSensorStartRequest(const aap_protobuf::service::sensorsource::message::SensorRequest& request) override;
    void onKeyBindingRequest(const aap_protobuf::service::media::sink::message::KeyBindingRequest& request) override;
    void onAudioFocusRequest(const aap_protobuf::service::control::message::AudioFocusRequest& request) override;
    void onNavigationFocusRequest(const aap_protobuf::service::control::message::NavFocusRequestNotification& request) override;
    void onPingRequest(const aap_protobuf::service::control::message::PingRequest& request) override;
    void onPingResponse(const aap_protobuf::service::control::message::PingResponse& response) override;
    void onBatteryStatusNotification(const aap_protobuf::service::control::message::BatteryStatusNotification& notification) override;
    void onVoiceSessionRequest(const aap_protobuf::service::control::message::VoiceSessionNotification& request) override;
    void onByeByeRequest(const aap_protobuf::service::control::message::ByeByeRequest& request) override;
    void onByeByeResponse(const aap_protobuf::service::control::message::ByeByeResponse& response) override;
    void onChannelError(const aasdk::error::Error& error) override;

private:
    using Clock = std::chrono::steady_clock;

    boost::asio::io_context& m_io;
    boost::asio::io_context::strand m_strand;
    boost::asio::steady_timer m_timer;
    std::shared_ptr<aasdk::transport::ITransport> m_transport;
    Logger& m_logger;
    std::atomic_bool& m_isStopRequested;
    ProjectionCallbacks m_callbacks;
    std::shared_ptr<aasdk::messenger::Cryptor> m_cryptor;
    std::shared_ptr<aasdk::messenger::Messenger> m_messenger;
    std::shared_ptr<aasdk::channel::control::ControlServiceChannel> m_control;
    std::shared_ptr<aasdk::channel::mediasink::video::VideoMediaSinkService> m_video;
    std::shared_ptr<aasdk::channel::sensorsource::SensorSourceService> m_sensors;
    std::shared_ptr<aasdk::channel::inputsource::InputSourceService> m_input;
    std::vector<std::shared_ptr<AudioSinkChannel>> m_audio;
    std::shared_ptr<MicrophoneChannel> m_microphone;
    std::unique_ptr<VideoDecoder> m_decoder;
    ProjectionResult m_result;
    Clock::time_point m_started{Clock::now()};
    Clock::time_point m_lastPing{};
    Clock::time_point m_lastActivity{Clock::now()};
    Clock::time_point m_stopDeadline{};
    Clock::time_point m_lastVersionRequest{};
    int m_versionRequests{1};
    int m_videoSession{-1};
    int m_decodeFailures{};
    std::uint64_t m_inputToken{};
    bool m_isInputReady{};
    bool m_isEnding{};
    bool m_isStopping{};
    bool m_isAuthenticated{};
    bool m_hasVersionReply{};
    bool m_hasVideoPackets{};
    bool m_isDiscoverySent{};
    bool m_hasPingReply{};

    void StartChannels();
    void AttachInput();
    void ShowFrame(VideoFrame frame);
    aasdk::channel::SendPromise::Pointer Promise();
    aasdk::channel::SendPromise::Pointer Promise(std::function<void()> onSent);
    void ReceiveControl();
    void MarkAlive();
    void FocusVideo();
    void Status(const std::string& message);
    std::string StartupTimeoutMessage() const;
    void Tick();
    bool RunWatchdogs(Clock::time_point now);
    void RepeatVersionRequest(Clock::time_point now);
    void SendKeepalive(Clock::time_point now);
    void ScheduleTick();
};
}
