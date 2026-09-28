#include "media/AudioPlayer.h"
#include "media/StreamText.h"
#include "video/FfmpegHandles.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
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
// How much decoded audio may wait in the output (which holds 500 ms): enough to ride out a busy moment, little enough
// that pause and stop answer at once.
constexpr std::size_t kLeadBytes = 48000 * kFrameBytes * 250 / 1000;
// Audio is written in slices of at most this, each waiting for room first.
constexpr std::size_t kSliceBytes = 48000 * kFrameBytes * 40 / 1000;
constexpr auto kProgressInterval = 500ms;

// Whether the source is an internet stream rather than a file.
bool IsNetworkSource(const std::string& source)
{
    return source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
}

// The first non-empty value of `key` in the two tag dictionaries (the file's, then the stream's).
std::string Tag(const AVDictionary* first, const AVDictionary* second, const char* key)
{
    for (const AVDictionary* dictionary : {first, second}) {
        if (const AVDictionaryEntry* entry = av_dict_get(dictionary, key, nullptr, 0); entry && entry->value && *entry->value) return entry->value;
    }
    return {};
}

// The options of an internet stream: the player names itself, asks for the "now playing" title and rides out short
// network drops.
AVDictionary* StreamOptions()
{
    AVDictionary* options = nullptr;
    av_dict_set(&options, "user_agent", "HeadUnit/0.1", 0);
    av_dict_set(&options, "icy", "1", 0);
    av_dict_set(&options, "reconnect", "1", 0);
    av_dict_set(&options, "reconnect_streamed", "1", 0);
    av_dict_set(&options, "reconnect_on_network_error", "1", 0);
    av_dict_set(&options, "reconnect_delay_max", "5", 0);
    av_dict_set(&options, "rw_timeout", "15000000", 0);   // microseconds without data before giving up
    av_dict_set(&options, "probesize", "131072", 0);
    av_dict_set(&options, "analyzeduration", "1500000", 0);
    return options;
}

// Converts decoded audio of any rate, sample format and channel layout to 48 kHz stereo 16-bit PCM. A source that
// changes its rate or layout midway (a radio stream switching programmes) gets a new resampler.
class PcmResampler {
public:
    PcmResampler() = default;
    PcmResampler(const PcmResampler&) = delete;
    PcmResampler& operator=(const PcmResampler&) = delete;

    // Releases the input layout it copied.
    ~PcmResampler() { av_channel_layout_uninit(&m_inLayout); }

    // `source` (or, with nullptr, what the resampler still holds) as PCM in `pcm`; false when it cannot be converted.
    bool Convert(const AVFrame* source, std::vector<std::uint8_t>& pcm)
    {
        pcm.clear();
        if (source && !Prepare(*source)) return false;
        if (!m_resampler) return true;
        const int room = swr_get_out_samples(m_resampler.get(), source ? source->nb_samples : 0);
        if (room <= 0) return true;
        pcm.resize(static_cast<std::size_t>(room) * kFrameBytes);
        std::uint8_t* planes[] = {pcm.data()};
        const int converted = swr_convert(m_resampler.get(), planes, room, source ? const_cast<const std::uint8_t**>(source->extended_data) : nullptr,
            source ? source->nb_samples : 0);
        if (converted < 0) {
            pcm.clear();
            return false;
        }
        pcm.resize(static_cast<std::size_t>(converted) * kFrameBytes);
        return true;
    }

private:
    SwrContextPtr m_resampler;
    AVChannelLayout m_inLayout{};
    int m_inRate{};
    int m_inFormat{-1};

    // Makes sure the resampler takes `source`'s rate, format and layout (a layout the source does not name is the
    // default one for its channel count).
    bool Prepare(const AVFrame& source)
    {
        AVChannelLayout layout{};
        if (source.ch_layout.order == AV_CHANNEL_ORDER_UNSPEC || source.ch_layout.nb_channels == 0)
            av_channel_layout_default(&layout, source.ch_layout.nb_channels > 0 ? source.ch_layout.nb_channels : 2);
        else
            av_channel_layout_copy(&layout, &source.ch_layout);
        const bool isSame = m_resampler && source.sample_rate == m_inRate && source.format == m_inFormat && av_channel_layout_compare(&layout, &m_inLayout) == 0;
        bool isReady = true;
        if (!isSame) {
            m_resampler.reset();
            SwrContext* rawResampler = nullptr;
            AVChannelLayout stereo{};
            av_channel_layout_default(&stereo, 2);
            const int result = swr_alloc_set_opts2(&rawResampler, &stereo, AV_SAMPLE_FMT_S16, static_cast<int>(kFormat.sampleRate), &layout,
                static_cast<AVSampleFormat>(source.format), source.sample_rate, 0, nullptr);
            av_channel_layout_uninit(&stereo);
            m_resampler.reset(rawResampler);
            if (result < 0 || swr_init(m_resampler.get()) < 0) {
                m_resampler.reset();
                isReady = false;
            } else {
                av_channel_layout_uninit(&m_inLayout);
                av_channel_layout_copy(&m_inLayout, &layout);
                m_inRate = source.sample_rate;
                m_inFormat = source.format;
            }
        }
        av_channel_layout_uninit(&layout);
        return isReady;
    }
};
}

// One Play: its source and generation, and the requests to stop or pause it (the worker and the caller share it).
struct AudioPlayer::Run {
    std::string source;
    unsigned generation{};
    std::atomic_bool isStopping{false};
    std::atomic_bool isPaused{false};
};

// FFmpeg's objects of one run, released in the right order however the run ends.
struct AudioPlayer::Decoding {
    AvFormatContextPtr format;
    AvCodecContextPtr codec;
    AvPacketPtr packet{av_packet_alloc()};
    AvFramePtr frame{av_frame_alloc()};
    PcmResampler resampler;
    int streamIndex{-1};
};

// Starts the worker; the output is opened on the first run.
AudioPlayer::AudioPlayer(AudioOpener opener, Logger& logger) : m_opener(std::move(opener)), m_logger(logger)
{
    static const int network = avformat_network_init();
    (void)network;
    m_thread = std::thread([this] { Worker(); });
}

// Stops what plays and ends the worker.
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

// Starts `source` (a file path in UTF-8 or an http(s) URL), ending whatever played before.
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

// Pauses or resumes. Pausing holds a file where it is; a live stream should rather be stopped.
void AudioPlayer::SetPaused(bool isPaused)
{
    m_logger.Write(LogLevel::Info, "MEDIA", isPaused ? "Paused" : "Resumed");
    {
        std::lock_guard lock(m_mutex);
        if (m_pending) m_pending->isPaused = isPaused;
        if (m_current) m_current->isPaused = isPaused;
    }
    m_wake.notify_all();
}

// Stops what plays; the output falls silent at once, not after what is queued.
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
    if (output) output->Flush();
}

// What the player does right now.
AudioPlayer::Status AudioPlayer::CurrentStatus() const
{
    std::lock_guard lock(m_mutex);
    return m_status;
}

// Whether a source is opening, playing or paused.
bool AudioPlayer::IsActive() const
{
    std::lock_guard lock(m_mutex);
    return m_status.state == State::Opening || m_status.state == State::Playing || m_status.state == State::Paused;
}

// The worker: plays the requested runs one after the other until the player ends.
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

// Plays one run from opening its source to its end, its failure or its stop.
void AudioPlayer::Decode(Run& run)
{
    const bool isStream = IsNetworkSource(run.source);
    Decoding decoding;
    if (!OpenSource(run, isStream, decoding)) return;
    const auto output = Output();
    if (!output) {
        Fail(run, "Keine Audioausgabe", AVERROR(ENODEV));
        return;
    }
    const AVStream* stream = decoding.format->streams[decoding.streamIndex];
    const double duration = !isStream && decoding.format->duration > 0 ? static_cast<double>(decoding.format->duration) / AV_TIME_BASE : 0.0;
    const std::string title = Tag(decoding.format->metadata, stream->metadata, "title");
    const std::string artist = Tag(decoding.format->metadata, stream->metadata, "artist");
    Update(run, [&](Status& status) {
        status.title = title;
        status.artist = artist;
        status.duration = duration;
    });
    m_logger.Write(LogLevel::Info, "MEDIA", "Playing " + run.source + ": " + decoding.codec->codec->name + ", " + std::to_string(decoding.codec->sample_rate) +
        " Hz, " + std::to_string(decoding.codec->ch_layout.nb_channels) + " channels" + (duration > 0 ? ", " + FormatPlayTime(duration) : std::string(", live")));
    PlayDecoded(run, isStream, duration, decoding, *output);
}

// Opens the source, finds its best audio stream and opens its decoder. Every blocking step (connecting, reading) asks
// the run whether to give up, so Stop and Play answer at once. False when the run ends here (failed or stopped).
bool AudioPlayer::OpenSource(Run& run, bool isStream, Decoding& decoding)
{
    decoding.format.reset(avformat_alloc_context());
    if (!decoding.format || !decoding.packet || !decoding.frame) {
        Fail(run, "Kein Speicher fuer den Decoder", AVERROR(ENOMEM));
        return false;
    }
    decoding.format->interrupt_callback.callback = [](void* opaque) { return static_cast<Run*>(opaque)->isStopping ? 1 : 0; };
    decoding.format->interrupt_callback.opaque = &run;
    AVDictionary* options = isStream ? StreamOptions() : nullptr;
    // A failed open frees the context and clears the pointer.
    AVFormatContext* rawFormat = decoding.format.release();
    int result = avformat_open_input(&rawFormat, run.source.c_str(), nullptr, &options);
    decoding.format.reset(rawFormat);
    av_dict_free(&options);
    if (run.isStopping) return false;
    if (result < 0) {
        Fail(run, isStream ? "Sender nicht erreichbar" : "Datei laesst sich nicht oeffnen", result);
        return false;
    }
    result = avformat_find_stream_info(decoding.format.get(), nullptr);
    if (run.isStopping) return false;
    if (result < 0) {
        Fail(run, isStream ? "Sender sendet keinen lesbaren Ton" : "Datei laesst sich nicht lesen", result);
        return false;
    }
    const AVCodec* codec = nullptr;
    decoding.streamIndex = av_find_best_stream(decoding.format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (decoding.streamIndex < 0 || !codec) {
        Fail(run, "Keine Tonspur gefunden", decoding.streamIndex < 0 ? decoding.streamIndex : AVERROR_DECODER_NOT_FOUND);
        return false;
    }
    // Only the sound is read; a cover picture (a video stream in many files) is skipped.
    for (unsigned index = 0; index < decoding.format->nb_streams; ++index) {
        if (static_cast<int>(index) != decoding.streamIndex) decoding.format->streams[index]->discard = AVDISCARD_ALL;
    }
    decoding.codec.reset(avcodec_alloc_context3(codec));
    if (!decoding.codec || (result = avcodec_parameters_to_context(decoding.codec.get(), decoding.format->streams[decoding.streamIndex]->codecpar)) < 0 ||
        (result = avcodec_open2(decoding.codec.get(), codec, nullptr)) < 0) {
        Fail(run, "Tonformat wird nicht unterstuetzt", decoding.codec ? result : AVERROR(ENOMEM));
        return false;
    }
    return true;
}

// Reads, decodes and plays the source until it ends, fails or the run is stopped. At the end of a file what the decoder
// and the resampler still hold is played, then the output plays out; a stream that ends has failed.
void AudioPlayer::PlayDecoded(Run& run, bool isStream, double duration, Decoding& decoding, IPcmOutput& output)
{
    std::uint64_t writtenBytes = 0;
    std::vector<std::uint8_t> pcm;
    auto lastProgress = std::chrono::steady_clock::now() - 10s;
    unsigned badPackets = 0;
    const auto decodeAll = [&] {
        while (avcodec_receive_frame(decoding.codec.get(), decoding.frame.get()) == 0) {
            const bool isConverted = decoding.resampler.Convert(decoding.frame.get(), pcm);
            av_frame_unref(decoding.frame.get());
            if (isConverted && !WriteBlock(run, output, pcm, writtenBytes)) return false;
        }
        return true;
    };
    while (!run.isStopping) {
        if (const auto now = std::chrono::steady_clock::now(); now - lastProgress >= kProgressInterval) {
            lastProgress = now;
            UpdateProgress(run, isStream, decoding, writtenBytes, output);
        }
        if (!WaitForRoom(run, output)) break;
        const int result = av_read_frame(decoding.format.get(), decoding.packet.get());
        if (result == AVERROR(EAGAIN)) {
            std::this_thread::sleep_for(10ms);
            continue;
        }
        if (result < 0) {
            if (run.isStopping) break;
            if (isStream) {
                Fail(run, result == AVERROR_EOF ? "Sender hat die Verbindung beendet" : "Verbindung zum Sender abgebrochen", result);
                return;
            }
            avcodec_send_packet(decoding.codec.get(), nullptr);
            if (!decodeAll()) break;
            if (decoding.resampler.Convert(nullptr, pcm) && !WriteBlock(run, output, pcm, writtenBytes)) break;
            while (!run.isStopping && output.Queued() > 0) std::this_thread::sleep_for(10ms);
            if (run.isStopping) break;
            Update(run, [&](Status& status) {
                status.state = State::Ended;
                status.position = duration > 0 ? duration : status.position;
            });
            return;
        }
        if (decoding.packet->stream_index == decoding.streamIndex) {
            const int sent = avcodec_send_packet(decoding.codec.get(), decoding.packet.get());
            // A damaged packet is skipped, as a player does; the log names the first ones.
            if (sent < 0 && sent != AVERROR(EAGAIN) && ++badPackets <= 3) m_logger.Write(LogLevel::Warning, "MEDIA", "Skipped a damaged packet: " + AvErrorText(sent));
        }
        av_packet_unref(decoding.packet.get());
        if (!decodeAll()) break;
    }
}

// Writes one converted block in slices, each once there is room; the first audio switches the status to Playing.
// False once the run is stopped.
bool AudioPlayer::WriteBlock(Run& run, IPcmOutput& output, std::span<const std::uint8_t> block, std::uint64_t& writtenBytes)
{
    while (!block.empty()) {
        if (!WaitForRoom(run, output)) return false;
        const std::size_t count = std::min(block.size(), kSliceBytes);
        output.Write(block.first(count));
        if (writtenBytes == 0) Update(run, [](Status& status) { if (status.state == State::Opening) status.state = State::Playing; });
        writtenBytes += count;
        block = block.subspan(count);
    }
    return true;
}

// The position (what the output has played so far) and, for a stream, what the station says is playing.
void AudioPlayer::UpdateProgress(const Run& run, bool isStream, Decoding& decoding, std::uint64_t writtenBytes, const IPcmOutput& output)
{
    const double position = static_cast<double>(writtenBytes - std::min<std::uint64_t>(writtenBytes, output.Queued())) / kFormat.BytesPerSecond();
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

// Changes the status, as long as `run` is the newest run (an older one must not overwrite what a newer one shows).
void AudioPlayer::Update(const Run& run, const std::function<void(Status&)>& change)
{
    std::lock_guard lock(m_mutex);
    if (m_status.generation == run.generation) change(m_status);
}

// The run failed: logged with FFmpeg's reason, and `message` goes to the user.
void AudioPlayer::Fail(const Run& run, const std::string& message, int code)
{
    m_logger.Write(LogLevel::Error, "MEDIA", message + " (" + run.source + "): " + AvErrorText(code));
    Update(run, [&](Status& status) {
        status.state = State::Failed;
        status.error = message;
    });
}

// The output, opened on the first run and kept for the next ones.
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
}
