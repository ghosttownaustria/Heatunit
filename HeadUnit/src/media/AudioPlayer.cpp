#include "media/AudioPlayer.h"
#include "media/StreamText.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <span>
#include <utility>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

namespace headunit {
namespace {
using namespace std::chrono_literals;
// Everything is played as the phone's media audio is: 48 kHz stereo, 16 bit.
const PcmFormat kFormat{48000, 2, 16};
constexpr std::size_t kFrameBytes = 4;
// How much decoded audio may wait in the output (which holds 500 ms): enough to ride out a busy moment, little
// enough that pause and stop answer at once.
constexpr std::size_t kLeadBytes = 48000 * kFrameBytes * 250 / 1000;
// Audio is written in slices of at most this, each waiting for room first.
constexpr std::size_t kSliceBytes = 48000 * kFrameBytes * 40 / 1000;

bool IsNetworkSource(const std::string& source)
{
    return source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
}
std::string AvError(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return text;
}
std::string Tag(const AVDictionary* first, const AVDictionary* second, const char* key)
{
    for (const AVDictionary* dictionary : {first, second})
        if (const AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0); entry && entry->value && *entry->value) return entry->value;
    return {};
}

// FFmpeg's objects of one run, released in the right order however the run ends.
struct Decoding {
    AVFormatContext* format{};
    AVCodecContext* codec{};
    AVPacket* packet{av_packet_alloc()};
    AVFrame* frame{av_frame_alloc()};
    SwrContext* resampler{};
    AVChannelLayout inLayout{};
    int inRate{}, inFormat{-1};
    ~Decoding() {
        swr_free(&resampler);
        av_channel_layout_uninit(&inLayout);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    // `source` (or, with nullptr, what the resampler still holds) as 48 kHz stereo 16-bit PCM. A source that changes
    // its rate or layout midway (a radio stream switching programmes) gets a new resampler.
    bool Convert(const AVFrame* source, std::vector<std::uint8_t>& pcm) {
        pcm.clear();
        if (source) {
            AVChannelLayout layout{};
            if (source->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || source->ch_layout.nb_channels == 0)
                av_channel_layout_default(&layout, source->ch_layout.nb_channels > 0 ? source->ch_layout.nb_channels : 2);
            else
                av_channel_layout_copy(&layout, &source->ch_layout);
            if (!resampler || source->sample_rate != inRate || source->format != inFormat || av_channel_layout_compare(&layout, &inLayout) != 0) {
                swr_free(&resampler);
                AVChannelLayout stereo{};
                av_channel_layout_default(&stereo, 2);
                const int result = swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_S16, static_cast<int>(kFormat.sampleRate), &layout,
                    static_cast<AVSampleFormat>(source->format), source->sample_rate, 0, nullptr);
                av_channel_layout_uninit(&stereo);
                if (result < 0 || swr_init(resampler) < 0) { av_channel_layout_uninit(&layout); swr_free(&resampler); return false; }
                av_channel_layout_uninit(&inLayout);
                av_channel_layout_copy(&inLayout, &layout);
                inRate = source->sample_rate;
                inFormat = source->format;
            }
            av_channel_layout_uninit(&layout);
        }
        if (!resampler) return true;
        const int room = swr_get_out_samples(resampler, source ? source->nb_samples : 0);
        if (room <= 0) return true;
        pcm.resize(static_cast<std::size_t>(room) * kFrameBytes);
        std::uint8_t* planes[] = {pcm.data()};
        const int converted = swr_convert(resampler, planes, room, source ? const_cast<const std::uint8_t**>(source->extended_data) : nullptr,
            source ? source->nb_samples : 0);
        if (converted < 0) { pcm.clear(); return false; }
        pcm.resize(static_cast<std::size_t>(converted) * kFrameBytes);
        return true;
    }
};
}

struct AudioPlayer::Run {
    std::string source;
    unsigned generation{};
    std::atomic_bool isStopping{false};
    std::atomic_bool isPaused{false};
};

AudioPlayer::AudioPlayer(AudioOpener opener, Logger& logger) : m_opener(std::move(opener)), m_logger(logger)
{
    static const int network = avformat_network_init();
    (void)network;
    m_thread = std::thread([this] { Worker(); });
}
AudioPlayer::~AudioPlayer()
{
    {
        std::lock_guard lock(m_mutex);
        m_isQuitting = true;
        if (m_current) m_current->isStopping = true;
    }
    m_wake.notify_all();
    m_thread.join();
}
void AudioPlayer::Play(const std::string& source)
{
    {
        std::lock_guard lock(m_mutex);
        if (m_current) m_current->isStopping = true;
        m_pending = std::make_shared<Run>();
        m_pending->source = source;
        m_pending->generation = m_status.generation + 1;
        m_status = Status{};
        m_status.state = State::Opening;
        m_status.source = source;
        m_status.generation = m_pending->generation;
    }
    m_wake.notify_all();
}
void AudioPlayer::SetPaused(bool isPaused)
{
    m_logger.Write("INFO", "MEDIA", isPaused ? "Paused" : "Resumed");
    {
        std::lock_guard lock(m_mutex);
        if (m_pending) m_pending->isPaused = isPaused;
        if (m_current) m_current->isPaused = isPaused;
    }
    m_wake.notify_all();
}
void AudioPlayer::Stop()
{
    std::shared_ptr<IPcmOutput> output;
    {
        std::lock_guard lock(m_mutex);
        if (m_current) m_current->isStopping = true;
        m_pending.reset();
        const unsigned generation = m_status.generation + 1;
        m_status = Status{};
        m_status.generation = generation;
        output = m_output;
    }
    m_wake.notify_all();
    if (output) output->Flush();   // silent at once, not after what is queued
}
AudioPlayer::Status AudioPlayer::CurrentStatus() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}
bool AudioPlayer::IsActive() const
{
    std::lock_guard lock(m_mutex);
    return m_status.state == State::Opening || m_status.state == State::Playing || m_status.state == State::Paused;
}
void AudioPlayer::Update(const Run& run, const std::function<void(Status&)>& change)
{
    std::lock_guard lock(m_mutex);
    if (m_status.generation == run.generation) change(m_status);
}
void AudioPlayer::Worker()
{
    for (;;) {
        std::shared_ptr<Run> run;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [this] { return m_isQuitting || m_pending; });
            if (m_isQuitting) return;
            run = std::exchange(m_pending, nullptr);
            m_current = run;
        }
        Decode(*run);
        std::shared_ptr<IPcmOutput> output;
        {
            std::lock_guard lock(m_mutex);
            m_current.reset();
            output = m_output;
        }
        // Whatever an aborted run left queued must not play into the next one.
        if (output && run->isStopping) output->Flush();
    }
}
std::shared_ptr<IPcmOutput> AudioPlayer::Output()
{
    {
        std::lock_guard lock(m_mutex);
        if (m_output) return m_output;
    }
    auto output = m_opener ? m_opener(AudioKind::Media, kFormat) : nullptr;
    std::lock_guard lock(m_mutex);
    m_output = output;
    return m_output;
}
// Waits until the output has room for more; while paused it waits for the resume. False once the run is stopped.
bool AudioPlayer::WaitForRoom(Run& run, IPcmOutput& output)
{
    while (!run.isStopping) {
        if (run.isPaused) {
            Update(run, [](Status& status) { status.state = State::Paused; });
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&run] { return run.isStopping || !run.isPaused; });
            lock.unlock();
            Update(run, [](Status& status) { if (status.state == State::Paused) status.state = State::Playing; });
            continue;
        }
        if (output.Queued() <= kLeadBytes) return true;
        std::this_thread::sleep_for(5ms);
    }
    return false;
}
void AudioPlayer::Decode(Run& run)
{
    const bool isStream = IsNetworkSource(run.source);
    const auto fail = [&](const std::string& message, int code) {
        m_logger.Write("ERROR", "MEDIA", message + " (" + run.source + "): " + AvError(code));
        Update(run, [&](Status& status) { status.state = State::Failed; status.error = message; });
    };
    Decoding decoding;
    decoding.format = avformat_alloc_context();
    if (!decoding.format || !decoding.packet || !decoding.frame) { fail("Kein Speicher fuer den Decoder", AVERROR(ENOMEM)); return; }
    // Every blocking step (connecting, reading) asks this whether to give up: Stop and Play answer at once.
    decoding.format->interrupt_callback.callback = [](void* opaque) { return static_cast<Run*>(opaque)->isStopping ? 1 : 0; };
    decoding.format->interrupt_callback.opaque = &run;
    AVDictionary* options = nullptr;
    if (isStream) {
        av_dict_set(&options, "user_agent", "HeadUnit/0.1", 0);
        av_dict_set(&options, "icy", "1", 0);                         // ask for the "now playing" title
        av_dict_set(&options, "reconnect", "1", 0);                   // ride out short network drops
        av_dict_set(&options, "reconnect_streamed", "1", 0);
        av_dict_set(&options, "reconnect_on_network_error", "1", 0);
        av_dict_set(&options, "reconnect_delay_max", "5", 0);
        av_dict_set(&options, "rw_timeout", "15000000", 0);           // microseconds without data before giving up
        av_dict_set(&options, "probesize", "131072", 0);
        av_dict_set(&options, "analyzeduration", "1500000", 0);
    }
    int result = avformat_open_input(&decoding.format, run.source.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (run.isStopping) return;
    if (result < 0) { fail(isStream ? "Sender nicht erreichbar" : "Datei laesst sich nicht oeffnen", result); return; }
    result = avformat_find_stream_info(decoding.format, nullptr);
    if (run.isStopping) return;
    if (result < 0) { fail(isStream ? "Sender sendet keinen lesbaren Ton" : "Datei laesst sich nicht lesen", result); return; }
    const AVCodec* codec = nullptr;
    const int index = av_find_best_stream(decoding.format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (index < 0 || !codec) { fail("Keine Tonspur gefunden", index < 0 ? index : AVERROR_DECODER_NOT_FOUND); return; }
    // Only the sound is read; a cover picture (a video stream in many files) is skipped.
    for (unsigned i = 0; i < decoding.format->nb_streams; ++i)
        if (static_cast<int>(i) != index) decoding.format->streams[i]->discard = AVDISCARD_ALL;
    AVStream* stream = decoding.format->streams[index];
    decoding.codec = avcodec_alloc_context3(codec);
    if (!decoding.codec || (result = avcodec_parameters_to_context(decoding.codec, stream->codecpar)) < 0 ||
        (result = avcodec_open2(decoding.codec, codec, nullptr)) < 0) {
        fail("Tonformat wird nicht unterstuetzt", decoding.codec ? result : AVERROR(ENOMEM));
        return;
    }
    const auto output = Output();
    if (!output) { fail("Keine Audioausgabe", AVERROR(ENODEV)); return; }
    const double duration = !isStream && decoding.format->duration > 0 ? static_cast<double>(decoding.format->duration) / AV_TIME_BASE : 0.0;
    const std::string title = Tag(decoding.format->metadata, stream->metadata, "title");
    const std::string artist = Tag(decoding.format->metadata, stream->metadata, "artist");
    Update(run, [&](Status& status) { status.title = title; status.artist = artist; status.duration = duration; });
    m_logger.Write("INFO", "MEDIA", "Playing " + run.source + ": " + codec->name + ", " + std::to_string(decoding.codec->sample_rate) + " Hz, " +
        std::to_string(decoding.codec->ch_layout.nb_channels) + " channels" + (duration > 0 ? ", " + FormatPlayTime(duration) : std::string(", live")));

    std::uint64_t writtenBytes = 0;
    bool hasStarted = false;
    std::vector<std::uint8_t> pcm;
    auto lastLook = std::chrono::steady_clock::now() - 10s;
    unsigned badPackets = 0;
    // Writes one converted block in slices, each once there is room. False once the run is stopped.
    const auto play = [&](std::span<const std::uint8_t> block) {
        while (!block.empty()) {
            if (!WaitForRoom(run, *output)) return false;
            const std::size_t count = std::min(block.size(), kSliceBytes);
            output->Write(block.first(count));
            writtenBytes += count;
            block = block.subspan(count);
            if (!hasStarted) { hasStarted = true; Update(run, [](Status& status) { if (status.state == State::Opening) status.state = State::Playing; }); }
        }
        return true;
    };
    const auto decodeAll = [&]() {
        while (avcodec_receive_frame(decoding.codec, decoding.frame) == 0) {
            const bool isConverted = decoding.Convert(decoding.frame, pcm);
            av_frame_unref(decoding.frame);
            if (!isConverted) continue;
            if (!play(pcm)) return false;
        }
        return true;
    };
    while (!run.isStopping) {
        const auto now = std::chrono::steady_clock::now();
        if (now - lastLook >= 500ms) {
            lastLook = now;
            const double position = static_cast<double>(writtenBytes - std::min<std::size_t>(writtenBytes, output->Queued())) / kFormat.BytesPerSecond();
            std::string streamTitle;
            if (isStream) {
                std::uint8_t* packet = nullptr;
                if (decoding.format->pb && av_opt_get(decoding.format->pb, "icy_metadata_packet", AV_OPT_SEARCH_CHILDREN, &packet) >= 0 && packet) {
                    streamTitle = IcyStreamTitle(reinterpret_cast<const char*>(packet));
                    av_free(packet);
                }
                if (streamTitle.empty()) streamTitle = Tag(decoding.format->metadata, nullptr, "StreamTitle");
            }
            Update(run, [&](Status& status) {
                status.position = position;
                if (!streamTitle.empty()) status.streamTitle = streamTitle;
            });
        }
        if (!WaitForRoom(run, *output)) break;
        result = av_read_frame(decoding.format, decoding.packet);
        if (result == AVERROR(EAGAIN)) { std::this_thread::sleep_for(10ms); continue; }
        if (result < 0) {
            if (run.isStopping) break;
            if (isStream) { fail(result == AVERROR_EOF ? "Sender hat die Verbindung beendet" : "Verbindung zum Sender abgebrochen", result); return; }
            // End of the file: what the decoder and the resampler still hold, then let the output play out.
            avcodec_send_packet(decoding.codec, nullptr);
            if (!decodeAll()) break;
            if (decoding.Convert(nullptr, pcm) && !play(pcm)) break;
            while (!run.isStopping && output->Queued() > 0) std::this_thread::sleep_for(10ms);
            if (run.isStopping) break;
            Update(run, [&](Status& status) { status.state = State::Ended; status.position = duration > 0 ? duration : status.position; });
            return;
        }
        if (decoding.packet->stream_index == index) {
            result = avcodec_send_packet(decoding.codec, decoding.packet);
            // A damaged packet is skipped, as a player does; the log names the first ones.
            if (result < 0 && result != AVERROR(EAGAIN) && ++badPackets <= 3) m_logger.Write("WARN", "MEDIA", "Skipped a damaged packet: " + AvError(result));
        }
        av_packet_unref(decoding.packet);
        if (!decodeAll()) break;
    }
}
}
