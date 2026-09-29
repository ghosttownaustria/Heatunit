#include "androidauto/ProjectionSession.h"
#include "androidauto/DisplayService.h"
#include "androidauto/InputReports.h"
#include "androidauto/ServiceDiscovery.h"
#include <aasdk/Messenger/MessageInStream.hpp>
#include <aasdk/Messenger/MessageOutStream.hpp>
#include <aasdk/Transport/SSLWrapper.hpp>
#include <aap_protobuf/service/sensorsource/message/DrivingStatus.pb.h>
#include <utility>

namespace headunit {
namespace {
namespace control = aap_protobuf::service::control::message;
namespace media = aap_protobuf::service::media::shared::message;
namespace video = aap_protobuf::service::media::video::message;
namespace sink = aap_protobuf::service::media::sink::message;
namespace source = aap_protobuf::service::media::source::message;
namespace sensor = aap_protobuf::service::sensorsource::message;
using aasdk::messenger::ChannelId;
constexpr auto kSuccess = aap_protobuf::shared::STATUS_SUCCESS;
constexpr const char* kStoppedByUser = "Android Auto stopped by user";
constexpr auto kTickInterval = std::chrono::milliseconds(100);
constexpr auto kPingInterval = std::chrono::seconds(5);
// How long the phone gets to answer the goodbye before the link is cut anyway.
constexpr auto kShutdownGrace = std::chrono::seconds(2);
// No byte from the phone for this long after discovery means the link is dead.
constexpr auto kSilenceTimeout = std::chrono::seconds(30);
constexpr auto kStartupTimeout = std::chrono::seconds(90);
constexpr auto kVersionTimeout = std::chrono::seconds(20);
constexpr auto kVersionRetryInterval = std::chrono::seconds(2);
constexpr int kMaxVersionRequests = 10;
// Undecodable packets are dropped (the stream recovers at the next keyframe); only a decoder that never recovers ends
// the session.
constexpr int kMaxDecodeFailures = 200;

// "(3/10)"
std::string Count(int number, int maximum)
{
    return "(" + std::to_string(number) + "/" + std::to_string(maximum) + ")";
}
}

// Builds the messenger over `transport` and the service channels; nothing is sent before Start.
ProjectionSession::ProjectionSession(boost::asio::io_context& io, std::shared_ptr<aasdk::transport::ITransport> transport, Logger& logger,
    std::atomic_bool& isStopRequested, ProjectionCallbacks callbacks)
    : m_io(io), m_strand(io), m_timer(io), m_transport(std::move(transport)), m_logger(logger), m_isStopRequested(isStopRequested),
      m_callbacks(std::move(callbacks))
{
    m_cryptor = std::make_shared<aasdk::messenger::Cryptor>(std::make_shared<aasdk::transport::SSLWrapper>());
    m_messenger = std::make_shared<aasdk::messenger::Messenger>(io, std::make_shared<aasdk::messenger::MessageInStream>(io, m_transport, m_cryptor),
        std::make_shared<aasdk::messenger::MessageOutStream>(io, m_transport, m_cryptor));
    m_control = std::make_shared<aasdk::channel::control::ControlServiceChannel>(m_strand, m_messenger);
    m_video = std::make_shared<aasdk::channel::mediasink::video::VideoMediaSinkService>(m_strand, m_messenger, ChannelId::MEDIA_SINK_VIDEO);
    m_sensors = std::make_shared<aasdk::channel::sensorsource::SensorSourceService>(m_strand, m_messenger);
    m_input = std::make_shared<aasdk::channel::inputsource::InputSourceService>(m_strand, m_messenger);
}

// Detaches from the window's input and releases the TLS state.
ProjectionSession::~ProjectionSession()
{
    if (m_callbacks.input) m_callbacks.input->Detach(m_inputToken);
    m_cryptor->deinit();
}

// Starts the channels, sends the version request and starts the tick.
void ProjectionSession::Start()
{
    m_cryptor->init();
    StartVideoDecoding();
    StartChannels();
    AttachInput();
    m_video->receive(shared_from_this());
    m_sensors->receive(shared_from_this());
    m_input->receive(shared_from_this());
    MarkAlive();
    Status("Android Auto version request sent; waiting for phone");
    m_control->sendVersionRequest(Promise());
    m_lastVersionRequest = Clock::now();
    ReceiveControl();
    Tick();
}

// Ends the session with `reason` (once). Stopping the transport joins its workers and rejects everything still pending;
// the rejections are drained by io.run() in RunAndroidAutoSession.
void ProjectionSession::End(const std::string& reason)
{
    if (m_isEnding) return;
    m_isEnding = true;
    m_result.message = reason;
    Status(reason);
    m_timer.cancel();
    if (m_callbacks.input) m_callbacks.input->Detach(m_inputToken);
    m_transport->stop();
    m_messenger->stop();
}

// Ends the session politely: the phone is told to leave Android Auto, which lets it start a fresh session on the next
// connect. Falls back to a plain End() when the encrypted control channel is not up yet (and the tick ends it when the
// phone does not answer).
void ProjectionSession::BeginShutdown()
{
    if (m_isEnding || m_isStopping) return;
    m_result.isStoppedByUser = true;
    if (!m_isAuthenticated) {
        End(kStoppedByUser);
        return;
    }
    m_isStopping = true;
    m_stopDeadline = Clock::now() + kShutdownGrace;
    Status("Stopping Android Auto: saying goodbye to the phone");
    control::ByeByeRequest request;
    request.set_reason(control::USER_SELECTION);
    m_control->sendShutdownRequest(request, Promise());
}

// How the session went, so far.
ProjectionResult ProjectionSession::Result() const
{
    ProjectionResult result = m_result;
    result.hasVideo = m_hasDecodedFrame;
    return result;
}

// Sends one input event of the window to the phone, once the phone has opened the input channel.
void ProjectionSession::HandleInput(const InputEvent& event)
{
    if (m_isEnding || m_isStopping || !m_isInputReady) return;
    const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch());
    m_input->sendInputReport(BuildInputReport(event, static_cast<std::uint64_t>(now.count())), Promise());
}

// The phone's answer to the version request starts the TLS handshake. A repeated request can be answered twice; the
// handshake must start only once.
void ProjectionSession::onVersionResponse(std::uint16_t major, std::uint16_t minor, aap_protobuf::shared::MessageStatus status)
{
    if (m_hasVersionReply) {
        ReceiveControl();
        return;
    }
    if (status != kSuccess) {
        End("Phone rejected AA version: status=" + std::to_string(status));
        return;
    }
    m_hasVersionReply = true;
    m_result.hasVersionReply = true;
    Status("Phone accepted Android Auto " + std::to_string(major) + "." + std::to_string(minor) + "; starting TLS");
    m_cryptor->doHandshake();
    m_control->sendHandshake(m_cryptor->readHandshakeBuffer(), Promise());
    ReceiveControl();
}

// One step of the TLS handshake; once it is complete, authentication is confirmed to the phone.
void ProjectionSession::onHandshake(const aasdk::common::DataConstBuffer& payload)
{
    m_cryptor->writeHandshakeBuffer(payload);
    const bool isComplete = m_cryptor->doHandshake();
    auto outgoing = m_cryptor->readHandshakeBuffer();
    if (!outgoing.empty()) m_control->sendHandshake(std::move(outgoing), Promise());
    if (isComplete) {
        m_isAuthenticated = true;
        Status("Android Auto TLS handshake completed; sending authentication complete");
        control::AuthResponse auth;
        auth.set_status(kSuccess);
        m_control->sendAuthComplete(auth, Promise());
    }
    ReceiveControl();
}

// The phone asks which services the head unit offers: everything, for the display chosen for this session.
void ProjectionSession::onServiceDiscoveryRequest(const control::ServiceDiscoveryRequest& request)
{
    const DisplayConfig display = m_callbacks.display;
    if (!VideoResolutionOf(display)) {
        End("Unsupported display size " + DisplayText(display));
        return;
    }
    const VideoLayout layout = VideoLayoutOf(display);
    const std::string frame = layout.HasMargins() ? " (" + std::to_string(layout.codecWidth) + "x" + std::to_string(layout.codecHeight) + " frame, margins " +
        std::to_string(layout.marginWidth) + "x" + std::to_string(layout.marginHeight) + ")" : std::string();
    if (m_callbacks.onPhoneName) m_callbacks.onPhoneName(!request.device_name().empty() ? request.device_name() : request.label_text());
    Status("Service discovery received from " + request.device_name() + "; advertising video " + DisplayText(display) + " at " +
        std::to_string(DisplayDensity(display)) + " dpi" + frame + ", audio, microphone, sensors and touch");
    const auto response = BuildServiceDiscoveryResponse(display);
    if (!response.IsInitialized()) {
        End("Invalid service description: " + response.InitializationErrorString());
        return;
    }
    Status("Service description ready: " + std::to_string(response.channels_size()) + " channels, " + std::to_string(response.ByteSizeLong()) + " bytes");
    m_control->sendServiceDiscoveryResponse(response, Promise([self = shared_from_this()] {
        self->m_isDiscoverySent = true;
        self->Status("Service discovery response sent; waiting for phone to open video channel");
    }));
    ReceiveControl();
}

// The phone opens the video, sensor or input channel; any other channel here is unexpected and ends the session.
void ProjectionSession::onChannelOpenRequest(const control::ChannelOpenRequest& request)
{
    Status("Phone opened AA channel " + std::to_string(request.service_id()));
    const auto response = SuccessfulOpenResponse();
    if (request.service_id() == static_cast<unsigned>(ChannelId::MEDIA_SINK_VIDEO)) {
        m_video->sendChannelOpenResponse(response, Promise());
        m_video->receive(shared_from_this());
    } else if (request.service_id() == static_cast<unsigned>(ChannelId::SENSOR)) {
        m_sensors->sendChannelOpenResponse(response, Promise());
        m_sensors->receive(shared_from_this());
    } else if (request.service_id() == static_cast<unsigned>(ChannelId::INPUT_SOURCE)) {
        m_isInputReady = true;
        m_input->sendChannelOpenResponse(response, Promise());
        m_input->receive(shared_from_this());
    } else {
        End("Unexpected AA channel open");
    }
}

// The phone sets the video channel up; the head unit takes the video focus once that is confirmed.
void ProjectionSession::onMediaChannelSetupRequest(const media::Setup& request)
{
    if (request.type() != media::MEDIA_CODEC_VIDEO_H264_BP) {
        End("Phone requested an unadvertised video codec");
        return;
    }
    Status("H.264 video channel configured");
    m_video->sendChannelSetupResponse(ReadyMediaConfig(), Promise([self = shared_from_this()] { self->FocusVideo(); }));
    m_video->receive(shared_from_this());
}

// The phone starts its video stream; its session id goes into the acknowledgements.
void ProjectionSession::onMediaChannelStartIndication(const media::Start& start)
{
    m_videoSession = start.session_id();
    Status("Phone started H.264 stream; session=" + std::to_string(m_videoSession));
    m_video->receive(shared_from_this());
}

// The phone stops its video stream.
void ProjectionSession::onMediaChannelStopIndication(const media::Stop&)
{
    Status("Phone stopped its video stream");
    m_videoSession = -1;
    m_video->receive(shared_from_this());
}

// A video packet: handed to the decode thread and acknowledged at once, so that the phone keeps streaming while it is
// decoded. An undecodable packet is acknowledged too, so that the phone can send the next keyframe.
void ProjectionSession::onMediaWithTimestampIndication(aasdk::messenger::Timestamp::ValueType, const aasdk::common::DataConstBuffer& buffer)
{
    MarkAlive();
    if (m_isEnding) return;
    if (!m_hasVideoPackets) {
        m_hasVideoPackets = true;
        Status("First real H.264 payload received: " + std::to_string(buffer.size) + " bytes");
    }
    m_decoder->Submit(std::vector<std::uint8_t>(buffer.cdata, buffer.cdata + buffer.size));
    source::Ack ack;
    ack.set_session_id(m_videoSession);
    ack.set_ack(1);
    m_video->sendMediaAckIndication(ack, Promise());
    m_video->receive(shared_from_this());
}

// A video packet without a timestamp: handled like one with.
void ProjectionSession::onMediaIndication(const aasdk::common::DataConstBuffer& buffer)
{
    onMediaWithTimestampIndication(0, buffer);
}

// The phone asks for the video focus: granted. When it asks for the car's own screen (the exit button in its launcher),
// the window shows the radio's home menu; the picture keeps coming, so going back to the phone needs nothing more.
void ProjectionSession::onVideoFocusRequest(const video::VideoFocusRequestNotification& request)
{
    if (request.mode() == video::VIDEO_FOCUS_NATIVE) {
        Status("The phone asked for the car's own screen");
        if (m_callbacks.onNativeScreen) m_callbacks.onNativeScreen();
    }
    FocusVideo();
    m_video->receive(shared_from_this());
}

// The phone starts a sensor: confirmed, then its one value is sent (driving unrestricted, day mode).
void ProjectionSession::onSensorStartRequest(const sensor::SensorRequest& request)
{
    sensor::SensorStartResponseMessage response;
    response.set_status(kSuccess);
    m_sensors->sendSensorStartResponse(response, Promise([self = shared_from_this(), type = request.type()] {
        sensor::SensorBatch batch;
        if (type == sensor::SENSOR_DRIVING_STATUS_DATA) batch.add_driving_status_data()->set_status(sensor::DRIVE_STATUS_UNRESTRICTED);
        else if (type == sensor::SENSOR_NIGHT_MODE) batch.add_night_mode_data()->set_night_mode(false);
        self->m_sensors->sendSensorEventIndication(batch, self->Promise());
    }));
    m_sensors->receive(shared_from_this());
}

// The phone binds the car keys: confirmed; from now on touch, keys and rotary input reach the phone.
void ProjectionSession::onKeyBindingRequest(const sink::KeyBindingRequest& request)
{
    Status("Phone binds " + std::to_string(request.keycodes_size()) + " car keys; touch, keys and rotary input are ready");
    sink::KeyBindingResponse response;
    response.set_status(kSuccess);
    m_input->sendKeyBindingResponse(response, Promise());
    m_input->receive(shared_from_this());
}

// Granting audio focus is what lets the phone start playback; a permanent loss makes it treat the head unit as unable to
// render its session, so only a release is answered with a loss.
void ProjectionSession::onAudioFocusRequest(const control::AudioFocusRequest& request)
{
    control::AudioFocusNotification response;
    response.set_focus_state(request.audio_focus_type() == control::AUDIO_FOCUS_RELEASE ? control::AUDIO_FOCUS_STATE_LOSS : control::AUDIO_FOCUS_STATE_GAIN);
    m_control->sendAudioFocusResponse(response, Promise());
    ReceiveControl();
}

// The phone asks for the navigation focus: it gets it (the phone's navigation is shown).
void ProjectionSession::onNavigationFocusRequest(const control::NavFocusRequestNotification&)
{
    control::NavFocusNotification response;
    response.set_focus_type(control::NAV_FOCUS_PROJECTED);
    m_control->sendNavigationFocusResponse(response, Promise());
    ReceiveControl();
}

// The phone's keepalive: answered with its own timestamp and data.
void ProjectionSession::onPingRequest(const control::PingRequest& request)
{
    control::PingResponse response;
    response.set_timestamp(request.timestamp());
    if (request.has_data()) response.set_data(request.data());
    m_control->sendPingResponse(response, Promise());
    ReceiveControl();
}

// The phone answers the head unit's keepalive (the first answer is logged).
void ProjectionSession::onPingResponse(const control::PingResponse&)
{
    if (!m_hasPingReply) {
        m_hasPingReply = true;
        Status("Phone acknowledged AA keepalive after service discovery");
    }
    ReceiveControl();
}

// Battery news from the phone: nothing to do.
void ProjectionSession::onBatteryStatusNotification(const control::BatteryStatusNotification&)
{
    ReceiveControl();
}

// The phone's voice session: nothing to do (no microphone samples are produced).
void ProjectionSession::onVoiceSessionRequest(const control::VoiceSessionNotification&)
{
    ReceiveControl();
}

// The phone ends Android Auto: acknowledged, then the session ends.
void ProjectionSession::onByeByeRequest(const control::ByeByeRequest& request)
{
    Status("Phone is ending Android Auto; reason=" + control::ByeByeReason_Name(request.reason()));
    control::ByeByeResponse response;
    m_control->sendShutdownResponse(response, Promise([self = shared_from_this()] { self->End("Phone ended Android Auto"); }));
}

// The phone acknowledged the head unit's goodbye.
void ProjectionSession::onByeByeResponse(const control::ByeByeResponse&)
{
    End(m_isStopping ? kStoppedByUser : "Android Auto shutdown acknowledged");
}

// A failure of any channel of the session ends it.
void ProjectionSession::onChannelError(const aasdk::error::Error& error)
{
    if (!m_isEnding) End(std::string("Android Auto channel failure: ") + error.what());
}

// Creates and starts the audio sinks and the microphone. They report to the window through a weak reference, so they
// never keep the session alive.
void ProjectionSession::StartChannels()
{
    auto report = [weak = weak_from_this()](const std::string& message) {
        if (auto self = weak.lock()) self->Status(message);
    };
    for (const auto& stream : kAudioStreams) {
        auto channel = std::make_shared<AudioSinkChannel>(m_strand, m_messenger, stream, m_callbacks.openAudio, report);
        channel->Start();
        m_audio.push_back(std::move(channel));
    }
    m_microphone = std::make_shared<MicrophoneChannel>(m_strand, m_messenger, report);
    m_microphone->Start();
}

// Takes the window's input for the session's lifetime. The events are handed over to the protocol thread; nothing is
// sent before the phone opened the input channel.
void ProjectionSession::AttachInput()
{
    if (!m_callbacks.input) return;
    m_inputToken = m_callbacks.input->Attach([weak = weak_from_this()](const InputEvent& event) {
        if (auto self = weak.lock()) boost::asio::post(self->m_strand, [self, event] { self->HandleInput(event); });
    });
}

// Starts the thread that decodes the phone's video. Its failures are logged; a decoder that keeps failing ends the
// session (on the protocol strand, like every other end).
void ProjectionSession::StartVideoDecoding()
{
    auto decoder = std::make_shared<VideoDecoder>([this](VideoFrame frame) { ShowFrame(std::move(frame)); });
    m_decoder = std::make_unique<VideoDecodeWorker>(
        [decoder](std::span<const std::uint8_t> packet) { decoder->Decode(packet); },
        [this, weak = weak_from_this()](const std::string& reason, int consecutiveFailures) {
            if (consecutiveFailures == 1 || consecutiveFailures % 50 == 0) Status("Dropped undecodable video packet: " + reason);
            if (consecutiveFailures < kMaxDecodeFailures) return;
            boost::asio::post(m_strand, [weak, reason] {
                if (auto self = weak.lock()) self->End("Video decoding keeps failing: " + reason);
            });
        });
}

// A decoded frame goes to the window (from the decode thread); the first one is what makes the session a success.
void ProjectionSession::ShowFrame(VideoFrame frame)
{
    if (!m_hasDecodedFrame.exchange(true)) {
        Status("First real Android Auto video frame decoded: " + std::to_string(frame.width) + "x" + std::to_string(frame.height));
    }
    if (m_callbacks.onFrame) m_callbacks.onFrame(std::move(frame));
}

// A send promise that treats a failure as a channel error.
aasdk::channel::SendPromise::Pointer ProjectionSession::Promise()
{
    return Promise([] {});
}

// A send promise that calls `onSent` once the message is out and treats a failure as a channel error.
aasdk::channel::SendPromise::Pointer ProjectionSession::Promise(std::function<void()> onSent)
{
    return MakeSendPromise(m_strand, std::move(onSent), [self = shared_from_this()](const aasdk::error::Error& error) { self->onChannelError(error); });
}

// Listens for the next control message. Called after every inbound control message, so it doubles as the liveness
// signal.
void ProjectionSession::ReceiveControl()
{
    MarkAlive();
    if (!m_isEnding) m_control->receive(shared_from_this());
}

// Notes that the phone said something just now.
void ProjectionSession::MarkAlive()
{
    m_lastActivity = Clock::now();
}

// Tells the phone that its video is shown.
void ProjectionSession::FocusVideo()
{
    video::VideoFocusNotification focus;
    focus.set_focus(video::VIDEO_FOCUS_PROJECTED);
    focus.set_unsolicited(true);
    m_video->sendVideoFocusIndication(focus, Promise());
}

// Logs a step of the session and hands it to the window.
void ProjectionSession::Status(const std::string& message)
{
    m_logger.Write(LogLevel::Info, "AA", message);
    if (m_callbacks.onStatus) m_callbacks.onStatus(message);
}

// Why the session did not get going: the first stage the phone did not reach.
std::string ProjectionSession::StartupTimeoutMessage() const
{
    if (!m_hasVersionReply) return "The phone did not answer the Android Auto version request within 20 seconds (phone locked or Android Auto not started).";
    if (!m_isAuthenticated) return "The Android Auto TLS handshake did not finish within 90 seconds.";
    if (!m_isDiscoverySent) return "The phone did not request the service list within 90 seconds.";
    return "No decoded video within 90 seconds. Check phone consent/unlock prompts and the preceding AA stage.";
}

// The 100 ms tick: the user's stop request, the goodbye deadline and the watchdogs of a running session.
void ProjectionSession::Tick()
{
    if (m_isEnding) return;
    const auto now = Clock::now();
    if (m_isStopRequested) BeginShutdown();
    if (m_isEnding) return;
    if (m_isStopping) {
        if (now >= m_stopDeadline) {
            End(std::string(kStoppedByUser) + " (phone did not acknowledge)");
            return;
        }
    } else if (!RunWatchdogs(now)) {
        return;
    }
    ScheduleTick();
}

// Repeats the version request, keeps the link alive and ends a session that went silent or never got going. False once
// it ended the session. No answer at all means Android Auto is not running on the phone (locked phone, stale accessory);
// waiting longer will not change that, so it fails early and lets the caller restart the connection.
bool ProjectionSession::RunWatchdogs(Clock::time_point now)
{
    RepeatVersionRequest(now);
    SendKeepalive(now);
    if (m_isDiscoverySent && now - m_lastActivity > kSilenceTimeout) {
        End("The phone stopped responding (no data for " + std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kSilenceTimeout).count()) + " seconds)");
        return false;
    }
    if ((!m_hasVersionReply && now - m_started > kVersionTimeout) || (!m_hasDecodedFrame && now - m_started > kStartupTimeout)) {
        End(StartupTimeoutMessage());
        return false;
    }
    return true;
}

// Android Auto may not be listening yet when the accessory has just (re)started; its answer to an early request is
// lost, so the request is repeated until it replies.
void ProjectionSession::RepeatVersionRequest(Clock::time_point now)
{
    if (m_hasVersionReply || m_versionRequests >= kMaxVersionRequests || now - m_lastVersionRequest < kVersionRetryInterval) return;
    m_lastVersionRequest = now;
    ++m_versionRequests;
    Status("No version answer yet; asking the phone again " + Count(m_versionRequests, kMaxVersionRequests));
    m_control->sendVersionRequest(Promise());
}

// The head unit's own keepalive after discovery. The advertised interval is one second; this stays well below that
// rate, so that a slow phone is never mistaken for a dead link.
void ProjectionSession::SendKeepalive(Clock::time_point now)
{
    if (!m_isDiscoverySent || now - m_lastPing < kPingInterval) return;
    m_lastPing = now;
    control::PingRequest ping;
    ping.set_timestamp(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
    m_control->sendPingRequest(ping, Promise());
}

// The next tick in 100 ms; a session that is gone by then is not ticked.
void ProjectionSession::ScheduleTick()
{
    m_timer.expires_after(kTickInterval);
    m_timer.async_wait([weak = weak_from_this()](const boost::system::error_code& error) {
        if (error) return;
        if (auto self = weak.lock()) self->Tick();
    });
}
}
