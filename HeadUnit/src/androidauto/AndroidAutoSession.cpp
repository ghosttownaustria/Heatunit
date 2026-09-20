// Session orchestration adapted from OpenAuto (GPL-3.0-or-later),
// Copyright (C) 2018 f1x.studio (Michal Szwaj).
// Upstream revisions and local changes: third_party/aasdk/PATCHES.md.
#include "androidauto/AndroidAutoSession.h"
#include "androidauto/DisplayService.h"
#include "androidauto/InputReports.h"
#include <aasdk/Channel/Control/ControlServiceChannel.hpp>
#include <aasdk/Channel/Control/IControlServiceChannelEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Video/VideoMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Video/IVideoMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSink/Audio/AudioMediaSinkService.hpp>
#include <aasdk/Channel/MediaSink/Audio/IAudioMediaSinkServiceEventHandler.hpp>
#include <aasdk/Channel/MediaSource/MediaSourceService.hpp>
#include <aasdk/Channel/MediaSource/IMediaSourceServiceEventHandler.hpp>
#include <aasdk/Channel/SensorSource/SensorSourceService.hpp>
#include <aasdk/Channel/SensorSource/ISensorSourceServiceEventHandler.hpp>
#include <aasdk/Channel/InputSource/InputSourceService.hpp>
#include <aasdk/Channel/InputSource/IInputSourceServiceEventHandler.hpp>
#include <aasdk/Messenger/Cryptor.hpp>
#include <aasdk/Messenger/Messenger.hpp>
#include <aasdk/Messenger/MessageInStream.hpp>
#include <aasdk/Messenger/MessageOutStream.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>
#include <aasdk/Common/ModernLogger.hpp>

namespace headunit {
namespace {
namespace ctrl = aap_protobuf::service::control::message;
namespace media = aap_protobuf::service::media::shared::message;
namespace video = aap_protobuf::service::media::video::message;
namespace sink = aap_protobuf::service::media::sink::message;
namespace source = aap_protobuf::service::media::source::message;
namespace sensor = aap_protobuf::service::sensorsource::message;
using aasdk::messenger::ChannelId;
constexpr auto success = aap_protobuf::shared::STATUS_SUCCESS;

// The phone only starts projection when the headunit advertises a complete set of
// services. A video-only description is accepted by the discovery step and then
// silently abandoned, so the audio sinks and the microphone source are advertised
// and answered here even though this proof of concept plays no audio.
struct AudioStream {
    ChannelId channel;
    sink::AudioStreamType type;
    std::uint32_t samplingRate;
    std::uint32_t bits;
    std::uint32_t channels;
    const char* name;
    AudioKind kind;
};
const AudioStream kAudioStreams[] = {
    {ChannelId::MEDIA_SINK_MEDIA_AUDIO, sink::AUDIO_STREAM_MEDIA, 48000, 16, 2, "Media", AudioKind::Media},
    {ChannelId::MEDIA_SINK_GUIDANCE_AUDIO, sink::AUDIO_STREAM_GUIDANCE, 16000, 16, 1, "Guidance", AudioKind::Guidance},
    {ChannelId::MEDIA_SINK_SYSTEM_AUDIO, sink::AUDIO_STREAM_SYSTEM_AUDIO, 16000, 16, 1, "System", AudioKind::System},
};
constexpr const char* kStoppedByUser = "Android Auto stopped by user";
constexpr auto kPingInterval = std::chrono::seconds(5);
// How long the phone gets to answer our goodbye before the link is cut anyway.
constexpr auto kShutdownGrace = std::chrono::seconds(2);
// No byte from the phone for this long after discovery means the link is dead.
constexpr auto kSilenceTimeout = std::chrono::seconds(30);
constexpr auto kStartupTimeout = std::chrono::seconds(90);
constexpr auto kVersionTimeout = std::chrono::seconds(20);
constexpr auto kVersionRetryInterval = std::chrono::seconds(2);
constexpr int kMaxVersionRequests = 10;
// Undecodable packets are dropped (the stream recovers at the next keyframe); only
// a decoder that never recovers ends the session.
constexpr int kMaxDecodeFailures = 200;
constexpr std::uint32_t kMicrophoneSamplingRate = 16000;
constexpr std::uint32_t kMicrophoneBits = 16;
constexpr std::uint32_t kMicrophoneChannels = 1;

// Accepts an advertised audio sink, plays what the phone sends and acknowledges every payload.
// Without an output the samples are dropped, which keeps the channel alive; refusing the
// channel would end the phone's session.
class AudioSink final : public aasdk::channel::mediasink::audio::IAudioMediaSinkServiceEventHandler,
    public std::enable_shared_from_this<AudioSink> {
public:
    AudioSink(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger,
        const AudioStream& stream, AudioOpener opener, std::function<void(const std::string&)> report)
        : m_strand(strand), m_name(stream.name), m_kind(stream.kind), m_opener(std::move(opener)), m_report(std::move(report)),
          m_channel(std::make_shared<aasdk::channel::mediasink::audio::AudioMediaSinkService>(strand, std::move(messenger), stream.channel)) {
        m_format.sampleRate = stream.samplingRate;
        m_format.channels = stream.channels;
        m_format.bitsPerSample = stream.bits;
    }
    void Start() { m_channel->receive(shared_from_this()); }
    void onChannelOpenRequest(const ctrl::ChannelOpenRequest&) override {
        m_report("Phone opened the " + m_name + " audio channel");
        ctrl::ChannelOpenResponse response;
        response.set_status(success);
        m_channel->sendChannelOpenResponse(response, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaChannelSetupRequest(const media::Setup&) override {
        media::Config response;
        response.set_status(media::Config::STATUS_READY);
        response.set_max_unacked(1);
        response.add_configuration_indices(0);
        m_channel->sendChannelSetupResponse(response, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaChannelStartIndication(const media::Start& indication) override {
        m_session = indication.session_id();
        m_channel->receive(shared_from_this());
    }
    void onMediaChannelStopIndication(const media::Stop&) override {
        m_session = -1;
        if (m_output) m_output->Flush();
        m_channel->receive(shared_from_this());
    }
    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer) override {
        Play(buffer);
        source::Ack ack;
        ack.set_session_id(m_session);
        ack.set_ack(1);
        m_channel->sendMediaAckIndication(ack, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override { onMediaWithTimestampIndication(0, buffer); }
    void onChannelError(const aasdk::error::Error& error) override {
        // Audio is not required for projection; report it but keep the video session running.
        // An abort is just the session shutting down, not a failure worth reporting.
        if (error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) return;
        m_report(m_name + " audio channel stopped: " + error.what());
    }
private:
    void Play(const aasdk::common::DataConstBuffer& buffer) {
        if (!m_opener) return;
        if (!m_output) {
            // Opened on the first sample, so a stream the phone never uses never touches the speaker.
            m_output = m_opener(m_kind, m_format);
            m_report(m_name + " audio started: " + std::to_string(m_format.sampleRate) + " Hz, " + std::to_string(m_format.channels) +
                " channel(s)" + (m_output ? "" : " (no output device)"));
            if (!m_output) m_opener = nullptr;
        }
        if (m_output) m_output->Write({buffer.cdata, buffer.size});
    }
    aasdk::channel::SendPromise::Pointer Promise() {
        auto promise = aasdk::channel::SendPromise::defer(m_strand);
        promise->then([] {}, [self = shared_from_this()](auto error) { self->onChannelError(error); });
        return promise;
    }
    boost::asio::io_context::strand& m_strand;
    std::string m_name;
    AudioKind m_kind;
    PcmFormat m_format;
    AudioOpener m_opener;
    std::shared_ptr<IPcmOutput> m_output;
    std::function<void(const std::string&)> m_report;
    std::shared_ptr<aasdk::channel::mediasink::audio::AudioMediaSinkService> m_channel;
    int m_session{-1};
};

// Advertises a microphone so voice input is negotiable. No samples are produced.
class MicrophoneSource final : public aasdk::channel::mediasource::IMediaSourceServiceEventHandler,
    public std::enable_shared_from_this<MicrophoneSource> {
public:
    MicrophoneSource(boost::asio::io_context::strand& strand, aasdk::messenger::IMessenger::Pointer messenger,
        std::function<void(const std::string&)> report)
        : m_strand(strand), m_report(std::move(report)),
          m_channel(std::make_shared<aasdk::channel::mediasource::MediaSourceService>(strand, std::move(messenger), ChannelId::MEDIA_SOURCE_MICROPHONE)) {}
    void Start() { m_channel->receive(shared_from_this()); }
    void onChannelOpenRequest(const ctrl::ChannelOpenRequest&) override {
        m_report("Phone opened the microphone channel");
        ctrl::ChannelOpenResponse response;
        response.set_status(success);
        m_channel->sendChannelOpenResponse(response, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaChannelSetupRequest(const media::Setup&) override {
        media::Config response;
        response.set_status(media::Config::STATUS_READY);
        response.set_max_unacked(1);
        response.add_configuration_indices(0);
        m_channel->sendChannelSetupResponse(response, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaSourceOpenRequest(const source::MicrophoneRequest& request) override {
        source::MicrophoneResponse response;
        response.set_session_id(0);
        response.set_status(success);
        m_report(request.open() ? "Microphone open acknowledged; this build captures no audio"
                                : "Microphone close acknowledged");
        m_channel->sendMicrophoneOpenResponse(response, Promise());
        m_channel->receive(shared_from_this());
    }
    void onMediaChannelAckIndication(const source::Ack&) override { m_channel->receive(shared_from_this()); }
    void onChannelError(const aasdk::error::Error& error) override {
        if (error.getCode() == aasdk::error::ErrorCode::OPERATION_ABORTED) return;
        m_report(std::string("Microphone channel stopped: ") + error.what());
    }
private:
    aasdk::channel::SendPromise::Pointer Promise() {
        auto promise = aasdk::channel::SendPromise::defer(m_strand);
        promise->then([] {}, [self = shared_from_this()](auto error) { self->onChannelError(error); });
        return promise;
    }
    boost::asio::io_context::strand& m_strand;
    std::function<void(const std::string&)> m_report;
    std::shared_ptr<aasdk::channel::mediasource::MediaSourceService> m_channel;
};

class Session final : public aasdk::channel::control::IControlServiceChannelEventHandler,
    public aasdk::channel::mediasink::video::IVideoMediaSinkServiceEventHandler,
    public aasdk::channel::sensorsource::ISensorSourceServiceEventHandler,
    public aasdk::channel::inputsource::IInputSourceServiceEventHandler,
    public std::enable_shared_from_this<Session> {
public:
    Session(boost::asio::io_context& io, std::shared_ptr<aasdk::transport::ITransport> transport,
        Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
        : m_io(io), m_strand(io), m_timer(io), m_transport(std::move(transport)), m_logger(logger),
          m_isStopRequested(isStopRequested), m_callbacks(std::move(callbacks)) {
        m_cryptor = std::make_shared<aasdk::messenger::Cryptor>(std::make_shared<aasdk::transport::SSLWrapper>());
        m_messenger = std::make_shared<aasdk::messenger::Messenger>(io,
            std::make_shared<aasdk::messenger::MessageInStream>(io, m_transport, m_cryptor),
            std::make_shared<aasdk::messenger::MessageOutStream>(io, m_transport, m_cryptor));
        m_control = std::make_shared<aasdk::channel::control::ControlServiceChannel>(m_strand, m_messenger);
        m_video = std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(m_strand, m_messenger, ChannelId::MEDIA_SINK_VIDEO);
        m_sensors = std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(m_strand, m_messenger);
        m_input = std::make_shared<aasdk::channel::inputsource::InputSourceService>(m_strand, m_messenger);
    }
    ~Session() {
        if (m_callbacks.input) m_callbacks.input->Detach(m_inputToken);
        m_cryptor->deinit();
    }
    void Start() {
        m_cryptor->init();
        m_decoder = std::make_unique<VideoDecoder>([this](VideoFrame frame) {
            if (!m_result.hasVideo) {
                m_result.hasVideo = true;
                Status("First real Android Auto video frame decoded: " + std::to_string(frame.width) + "x" + std::to_string(frame.height));
            }
            if (m_callbacks.onFrame) m_callbacks.onFrame(std::move(frame));
        });
        auto report = [weak = weak_from_this()](const std::string& message) {
            if (auto self = weak.lock()) self->Status(message);
        };
        for (const auto& stream : kAudioStreams) {
            auto channel = std::make_shared<AudioSink>(m_strand, m_messenger, stream, m_callbacks.openAudio, report);
            channel->Start();
            m_audio.push_back(std::move(channel));
        }
        m_microphone = std::make_shared<MicrophoneSource>(m_strand, m_messenger, report);
        m_microphone->Start();
        if (m_callbacks.input) {
            // The window's events are handed over to the protocol thread; nothing is sent before the
            // phone opened the input channel.
            m_inputToken = m_callbacks.input->Attach([weak = weak_from_this()](const InputEvent& event) {
                if (auto self = weak.lock()) boost::asio::post(self->m_strand, [self, event] { self->HandleInput(event); });
            });
        }
        m_video->receive(shared_from_this());
        m_sensors->receive(shared_from_this());
        m_input->receive(shared_from_this());
        MarkAlive();
        Status("Android Auto version request sent; waiting for phone");
        m_control->sendVersionRequest(Promise());
        m_lastVersionRequest = std::chrono::steady_clock::now();
        ReceiveControl();
        Tick();
    }
    void End(const std::string& reason) {
        if (m_isEnding) return;
        m_isEnding = true;
        m_result.message = reason;
        Status(reason);
        m_timer.cancel();
        if (m_callbacks.input) m_callbacks.input->Detach(m_inputToken);
        // Stopping the transport joins its USB workers and rejects everything still
        // pending; the rejections are drained by io.run() in RunAndroidAutoSession.
        m_transport->stop();
        m_messenger->stop();
    }
    // Ends the session politely: the phone is told to leave Android Auto, which lets
    // it start a fresh session on the next connect. Falls back to a plain End() when
    // the encrypted control channel is not up yet or the phone does not answer.
    void BeginShutdown() {
        if (m_isEnding || m_isStopping) return;
        m_result.isStoppedByUser = true;
        if (!m_isAuthenticated) { End(kStoppedByUser); return; }
        m_isStopping = true;
        m_stopDeadline = std::chrono::steady_clock::now() + kShutdownGrace;
        Status("Stopping Android Auto: saying goodbye to the phone");
        ctrl::ByeByeRequest request;
        request.set_reason(ctrl::USER_SELECTION);
        m_control->sendShutdownRequest(request, Promise());
    }
    ProjectionResult Result() const { return m_result; }
    void HandleInput(const InputEvent& event) {
        if (m_isEnding || m_isStopping || !m_isInputReady) return;
        const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
        m_input->sendInputReport(BuildInputReport(event, static_cast<std::uint64_t>(now.count())), Promise());
    }
    void onVersionResponse(std::uint16_t major, std::uint16_t minor, aap_protobuf::shared::MessageStatus status) override {
        // A repeated request can be answered twice; the TLS handshake must start only once.
        if (m_hasVersionReply) { ReceiveControl(); return; }
        if (status != success) { End("Phone rejected AA version: status=" + std::to_string(status)); return; }
        m_hasVersionReply = true;
        m_result.hasVersionReply = true;
        Status("Phone accepted Android Auto " + std::to_string(major) + "." + std::to_string(minor) + "; starting TLS");
        m_cryptor->doHandshake();
        m_control->sendHandshake(m_cryptor->readHandshakeBuffer(), Promise());
        ReceiveControl();
    }
    void onHandshake(const aasdk::common::DataConstBuffer& payload) override {
        m_cryptor->writeHandshakeBuffer(payload);
        const bool isComplete = m_cryptor->doHandshake();
        auto outgoing = m_cryptor->readHandshakeBuffer();
        if (!outgoing.empty()) m_control->sendHandshake(std::move(outgoing), Promise());
        if (isComplete) {
            m_isAuthenticated = true;
            Status("Android Auto TLS handshake completed; sending authentication complete");
            ctrl::AuthResponse auth;
            auth.set_status(success);
            m_control->sendAuthComplete(auth, Promise());
        }
        ReceiveControl();
    }
    void onServiceDiscoveryRequest(const ctrl::ServiceDiscoveryRequest& request) override {
        const DisplayConfig display = m_callbacks.display;
        if (!VideoResolutionOf(display)) { End("Unsupported display size " + DisplayText(display)); return; }
        const VideoLayout layout = VideoLayoutOf(display);
        Status("Service discovery received from " + request.device_name() + "; advertising video " + DisplayText(display) + " at " +
            std::to_string(DisplayDensity(display)) + " dpi" +
            (layout.HasMargins() ? " (" + std::to_string(layout.codecWidth) + "x" + std::to_string(layout.codecHeight) + " frame, margins " +
                std::to_string(layout.marginWidth) + "x" + std::to_string(layout.marginHeight) + ")" : std::string()) +
            ", audio, microphone, sensors and touch");
        ctrl::ServiceDiscoveryResponse response;
        response.mutable_channels()->Reserve(8);
        response.set_display_name("HeadUnit Windows PoC");
        response.set_driver_position(ctrl::DRIVER_POSITION_LEFT);
        response.set_probe_for_support(false);
        auto* ping = response.mutable_connection_configuration()->mutable_ping_configuration();
        ping->set_tracked_ping_count(5);
        ping->set_timeout_ms(3000);
        ping->set_interval_ms(1000);
        ping->set_high_latency_threshold_ms(200);
        auto* info = response.mutable_headunit_info();
        info->set_make("HeadUnit"); info->set_model("Windows PoC"); info->set_year("2026");
        info->set_vehicle_id("HeadUnit-PoC-001"); info->set_head_unit_make("HeadUnit");
        info->set_head_unit_model("Windows"); info->set_head_unit_software_build("1"); info->set_head_unit_software_version("0.2");
        // Audio sinks first, in the order a known working headunit advertises them.
        for (const auto& stream : kAudioStreams) {
            auto* channel = response.add_channels();
            channel->set_id(static_cast<unsigned>(stream.channel));
            auto* service = channel->mutable_media_sink_service();
            service->set_available_type(media::MEDIA_CODEC_AUDIO_PCM);
            service->set_audio_type(stream.type);
            service->set_available_while_in_call(true);
            auto* configuration = service->add_audio_configs();
            configuration->set_sampling_rate(stream.samplingRate);
            configuration->set_number_of_bits(stream.bits);
            configuration->set_number_of_channels(stream.channels);
        }
        auto* channel = response.add_channels();
        channel->set_id(static_cast<unsigned>(ChannelId::MEDIA_SINK_VIDEO));
        auto* videoService = channel->mutable_media_sink_service();
        videoService->set_available_type(media::MEDIA_CODEC_VIDEO_H264_BP);
        videoService->set_available_while_in_call(true);
        *videoService->add_video_configs() = BuildVideoConfiguration(display);
        channel = response.add_channels();
        channel->set_id(static_cast<unsigned>(ChannelId::MEDIA_SOURCE_MICROPHONE));
        auto* microphone = channel->mutable_media_source_service();
        microphone->set_available_type(media::MEDIA_CODEC_AUDIO_PCM);
        auto* microphoneConfiguration = microphone->mutable_audio_config();
        microphoneConfiguration->set_sampling_rate(kMicrophoneSamplingRate);
        microphoneConfiguration->set_number_of_bits(kMicrophoneBits);
        microphoneConfiguration->set_number_of_channels(kMicrophoneChannels);
        channel = response.add_channels();
        channel->set_id(static_cast<unsigned>(ChannelId::SENSOR));
        channel->mutable_sensor_source_service()->add_sensors()->set_sensor_type(sensor::SENSOR_DRIVING_STATUS_DATA);
        channel->mutable_sensor_source_service()->add_sensors()->set_sensor_type(sensor::SENSOR_NIGHT_MODE);
        channel = response.add_channels();
        channel->set_id(static_cast<unsigned>(ChannelId::INPUT_SOURCE));
        auto* touch = channel->mutable_input_source_service()->add_touchscreen();
        // Touch positions are pixels of the shown area (the phone does not count the margins).
        touch->set_width(layout.width); touch->set_height(layout.height);
        touch->set_type(aap_protobuf::service::inputsource::message::CAPACITIVE);
        for (const auto keycode : keys::Supported) channel->mutable_input_source_service()->add_keycodes_supported(static_cast<int>(keycode));
        if (!response.IsInitialized()) { End("Invalid service description: " + response.InitializationErrorString()); return; }
        Status("Service description ready: " + std::to_string(response.channels_size()) + " channels, "
            + std::to_string(response.ByteSizeLong()) + " bytes");
        m_control->sendServiceDiscoveryResponse(response, Promise([self = shared_from_this()] {
            self->m_isDiscoverySent = true;
            self->Status("Service discovery response sent; waiting for phone to open video channel");
        }));
        ReceiveControl();
    }
    void onChannelOpenRequest(const ctrl::ChannelOpenRequest& request) override {
        Status("Phone opened AA channel " + std::to_string(request.service_id()));
        ctrl::ChannelOpenResponse response;
        response.set_status(success);
        if (request.service_id() == static_cast<unsigned>(ChannelId::MEDIA_SINK_VIDEO)) {
            m_video->sendChannelOpenResponse(response, Promise()); m_video->receive(shared_from_this());
        } else if (request.service_id() == static_cast<unsigned>(ChannelId::SENSOR)) {
            m_sensors->sendChannelOpenResponse(response, Promise()); m_sensors->receive(shared_from_this());
        } else if (request.service_id() == static_cast<unsigned>(ChannelId::INPUT_SOURCE)) {
            m_isInputReady = true;
            m_input->sendChannelOpenResponse(response, Promise()); m_input->receive(shared_from_this());
        } else End("Unexpected AA channel open");
    }
    void onMediaChannelSetupRequest(const media::Setup& request) override {
        if (request.type() != media::MEDIA_CODEC_VIDEO_H264_BP) { End("Phone requested an unadvertised video codec"); return; }
        Status("H.264 video channel configured");
        media::Config response;
        response.set_status(media::Config::STATUS_READY); response.set_max_unacked(1); response.add_configuration_indices(0);
        m_video->sendChannelSetupResponse(response, Promise([self = shared_from_this()] { self->FocusVideo(); }));
        m_video->receive(shared_from_this());
    }
    void onMediaChannelStartIndication(const media::Start& start) override {
        m_videoSession = start.session_id();
        Status("Phone started H.264 stream; session=" + std::to_string(m_videoSession));
        m_video->receive(shared_from_this());
    }
    void onMediaChannelStopIndication(const media::Stop&) override {
        Status("Phone stopped its video stream"); m_videoSession = -1; m_video->receive(shared_from_this());
    }
    void onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer) override {
        MarkAlive();
        if (m_isEnding) return;
        if (!m_hasVideoPackets) { m_hasVideoPackets = true; Status("First real H.264 payload received: " + std::to_string(buffer.size) + " bytes"); }
        try {
            m_decoder->Decode({buffer.cdata, buffer.size});
            m_decodeFailures = 0;
        } catch (const std::exception& error) {
            // The packet is still acknowledged so the phone keeps streaming and can
            // send the next keyframe.
            if (++m_decodeFailures == 1 || m_decodeFailures % 50 == 0)
                Status(std::string("Dropped undecodable video packet: ") + error.what());
            if (m_decodeFailures >= kMaxDecodeFailures) { End(std::string("Video decoding keeps failing: ") + error.what()); return; }
        }
        source::Ack ack;
        ack.set_session_id(m_videoSession); ack.set_ack(1);
        m_video->sendMediaAckIndication(ack, Promise());
        m_video->receive(shared_from_this());
    }
    void onMediaIndication(const aasdk::common::DataConstBuffer& buffer) override { onMediaWithTimestampIndication(0, buffer); }
    void onVideoFocusRequest(const video::VideoFocusRequestNotification&) override { FocusVideo(); m_video->receive(shared_from_this()); }
    void onSensorStartRequest(const sensor::SensorRequest& request) override {
        sensor::SensorStartResponseMessage response; response.set_status(success);
        m_sensors->sendSensorStartResponse(response, Promise([self = shared_from_this(), type = request.type()] {
            sensor::SensorBatch batch;
            if (type == sensor::SENSOR_DRIVING_STATUS_DATA) batch.add_driving_status_data()->set_status(sensor::DRIVE_STATUS_UNRESTRICTED);
            else if (type == sensor::SENSOR_NIGHT_MODE) batch.add_night_mode_data()->set_night_mode(false);
            self->m_sensors->sendSensorEventIndication(batch, self->Promise());
        }));
        m_sensors->receive(shared_from_this());
    }
    void onKeyBindingRequest(const sink::KeyBindingRequest& request) override {
        Status("Phone binds " + std::to_string(request.keycodes_size()) + " car keys; touch, keys and rotary input are ready");
        sink::KeyBindingResponse response; response.set_status(success);
        m_input->sendKeyBindingResponse(response, Promise()); m_input->receive(shared_from_this());
    }
    void onAudioFocusRequest(const ctrl::AudioFocusRequest& request) override {
        // Granting focus is what lets the phone start playback; a permanent loss makes
        // it treat the headunit as unable to render its session.
        ctrl::AudioFocusNotification response;
        response.set_focus_state(request.audio_focus_type() == ctrl::AUDIO_FOCUS_RELEASE
            ? ctrl::AUDIO_FOCUS_STATE_LOSS : ctrl::AUDIO_FOCUS_STATE_GAIN);
        m_control->sendAudioFocusResponse(response, Promise()); ReceiveControl();
    }
    void onNavigationFocusRequest(const ctrl::NavFocusRequestNotification&) override {
        ctrl::NavFocusNotification response; response.set_focus_type(ctrl::NAV_FOCUS_PROJECTED);
        m_control->sendNavigationFocusResponse(response, Promise()); ReceiveControl();
    }
    void onPingRequest(const ctrl::PingRequest& request) override {
        ctrl::PingResponse response; response.set_timestamp(request.timestamp());
        if (request.has_data()) response.set_data(request.data());
        m_control->sendPingResponse(response, Promise()); ReceiveControl();
    }
    void onPingResponse(const ctrl::PingResponse&) override {
        if (!m_hasPingReply) { m_hasPingReply = true; Status("Phone acknowledged AA keepalive after service discovery"); }
        ReceiveControl();
    }
    void onBatteryStatusNotification(const ctrl::BatteryStatusNotification&) override { ReceiveControl(); }
    void onVoiceSessionRequest(const ctrl::VoiceSessionNotification&) override { ReceiveControl(); }
    void onByeByeRequest(const ctrl::ByeByeRequest& request) override {
        Status("Phone is ending Android Auto; reason=" + ctrl::ByeByeReason_Name(request.reason()));
        ctrl::ByeByeResponse response;
        m_control->sendShutdownResponse(response, Promise([self = shared_from_this()] { self->End("Phone ended Android Auto"); }));
    }
    void onByeByeResponse(const ctrl::ByeByeResponse&) override {
        End(m_isStopping ? kStoppedByUser : "Android Auto shutdown acknowledged");
    }
    void onChannelError(const aasdk::error::Error& error) override { if (!m_isEnding) End(std::string("Android Auto channel failure: ") + error.what()); }
private:
    aasdk::channel::SendPromise::Pointer Promise(std::function<void()> onSent = [] {}) {
        auto promise = aasdk::channel::SendPromise::defer(m_strand);
        promise->then(std::move(onSent), [self = shared_from_this()](auto error) { self->onChannelError(error); });
        return promise;
    }
    // Called after every inbound control message, so it doubles as the liveness signal.
    void ReceiveControl() { MarkAlive(); if (!m_isEnding) m_control->receive(shared_from_this()); }
    void MarkAlive() { m_lastActivity = std::chrono::steady_clock::now(); }
    void FocusVideo() {
        video::VideoFocusNotification focus; focus.set_focus(video::VIDEO_FOCUS_PROJECTED); focus.set_unsolicited(true);
        m_video->sendVideoFocusIndication(focus, Promise());
    }
    void Status(const std::string& message) { m_logger.Write("INFO", "AA", message); if (m_callbacks.onStatus) m_callbacks.onStatus(message); }
    std::string StartupTimeoutMessage() const {
        if (!m_hasVersionReply) return "The phone did not answer the Android Auto version request within 20 seconds (phone locked or Android Auto not started).";
        if (!m_isAuthenticated) return "The Android Auto TLS handshake did not finish within 90 seconds.";
        if (!m_isDiscoverySent) return "The phone did not request the service list within 90 seconds.";
        return "No decoded video within 90 seconds. Check phone consent/unlock prompts and the preceding AA stage.";
    }
    void Tick() {
        if (m_isEnding) return;
        const auto now = std::chrono::steady_clock::now();
        if (m_isStopRequested) BeginShutdown();
        if (m_isEnding) return;
        if (m_isStopping) {
            if (now >= m_stopDeadline) { End(std::string(kStoppedByUser) + " (phone did not acknowledge)"); return; }
        } else {
            // Android Auto may not be listening yet when the accessory has just (re)started;
            // its answer to an early request is lost, so ask again until it replies.
            if (!m_hasVersionReply && m_versionRequests < kMaxVersionRequests && now - m_lastVersionRequest >= kVersionRetryInterval) {
                m_lastVersionRequest = now;
                ++m_versionRequests;
                Status("No version answer yet; asking the phone again (" + std::to_string(m_versionRequests) + "/" + std::to_string(kMaxVersionRequests) + ")");
                m_control->sendVersionRequest(Promise());
            }
            // The advertised interval is one second; the headunit's own keepalive stays well
            // below that so a slow phone is never mistaken for a dead link.
            if (m_isDiscoverySent && now - m_lastPing >= kPingInterval) {
                m_lastPing = now;
                ctrl::PingRequest ping;
                ping.set_timestamp(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
                m_control->sendPingRequest(ping, Promise());
            }
            if (m_isDiscoverySent && now - m_lastActivity > kSilenceTimeout) {
                End("The phone stopped responding (no data for " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kSilenceTimeout).count()) + " seconds)");
                return;
            }
            // No answer at all means Android Auto is not running on the phone (locked phone,
            // stale accessory); waiting longer will not change that, so fail early and let the
            // caller restart the connection instead of idling for the whole startup window.
            if (!m_hasVersionReply && now - m_started > kVersionTimeout) { End(StartupTimeoutMessage()); return; }
            if (!m_result.hasVideo && now - m_started > kStartupTimeout) { End(StartupTimeoutMessage()); return; }
        }
        m_timer.expires_after(std::chrono::milliseconds(100));
        m_timer.async_wait([weak = weak_from_this()](auto error) { if (!error) if (auto self = weak.lock()) self->Tick(); });
    }
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
    std::vector<std::shared_ptr<AudioSink>> m_audio;
    std::shared_ptr<MicrophoneSource> m_microphone;
    std::unique_ptr<VideoDecoder> m_decoder;
    ProjectionResult m_result;
    std::chrono::steady_clock::time_point m_started{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_lastPing{};
    std::chrono::steady_clock::time_point m_lastActivity{std::chrono::steady_clock::now()};
    std::chrono::steady_clock::time_point m_stopDeadline{};
    std::chrono::steady_clock::time_point m_lastVersionRequest{};
    int m_versionRequests{1};
    int m_videoSession{-1};
    int m_decodeFailures{};
    std::uint64_t m_inputToken{};
    bool m_isInputReady{};
    bool m_isEnding{}, m_isStopping{}, m_isAuthenticated{}, m_hasVersionReply{};
    bool m_hasVideoPackets{}, m_isDiscoverySent{}, m_hasPingReply{};
};
}
ProjectionResult RunAndroidAutoSession(std::shared_ptr<aasdk::transport::ITransport> transport,
    Logger& logger, std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks) {
    if (std::getenv("HEADUNIT_PROTOCOL_TRACE"))
        aasdk::common::ModernLogger::getInstance().setLevel(aasdk::common::LogLevel::DEBUG);
    boost::asio::io_context io;
    auto session = std::make_shared<Session>(io, transport, logger, isStopRequested, std::move(callbacks));
    const std::weak_ptr<Session> observer = session;
    try { session->Start(); }
    catch (const std::exception& error) { session->End(std::string("AA session failed: ") + error.what()); }
    // A throwing handler must not abandon the handlers still queued behind it: end the
    // session, then keep running until every completion has been delivered.
    for (bool isDrained = false; !isDrained;) {
        try { io.run(); isDrained = true; }
        catch (const std::exception& error) { session->End(std::string("AA session failed: ") + error.what()); }
    }
    transport->stop();
    io.restart();
    io.poll();
    auto result = session->Result();
    session.reset();
    // Everything the session owns must be gone before the caller releases the USB
    // interface; a survivor would mean a promise/handler ownership cycle.
    if (!observer.expired()) logger.Write("WARN", "AA", "Session object is still referenced after shutdown (ownership cycle)");
    return result;
}
}
