#include "video/VideoDecoder.h"
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace headunit {
namespace {
constexpr std::size_t kMaxPacketSize = 4 * 1024 * 1024;
constexpr int kMaxWidth = 1920;
constexpr int kMaxHeight = 1080;

// Throws when an FFmpeg call returned an error code.
void CheckAv(int code, const char* operation)
{
    if (code >= 0) return;
    throw std::runtime_error(std::string(operation) + ": " + AvErrorText(code));
}
}

// Opens FFmpeg's H.264 decoder (two slice threads); `onFrame` receives every decoded picture.
VideoDecoder::VideoDecoder(std::function<void(VideoFrame)> onFrame) : m_onFrame(std::move(onFrame))
{
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) throw std::runtime_error("FFmpeg H.264 decoder unavailable");
    m_codec.reset(avcodec_alloc_context3(codec));
    m_parser.reset(av_parser_init(AV_CODEC_ID_H264));
    m_frame.reset(av_frame_alloc());
    m_packet.reset(av_packet_alloc());
    if (!m_codec || !m_parser || !m_frame || !m_packet) throw std::runtime_error("H.264 decoder allocation failed");
    m_codec->thread_count = 2;
    m_codec->thread_type = FF_THREAD_SLICE;
    CheckAv(avcodec_open2(m_codec.get(), codec, nullptr), "H.264 decoder open");
}

// Parses `bytes` into access units and decodes each; every finished picture goes to the frame callback.
void VideoDecoder::Decode(std::span<const std::uint8_t> bytes)
{
    if (bytes.size() > kMaxPacketSize) throw std::runtime_error("Video packet exceeds limit");
    std::vector<std::uint8_t> padded(bytes.begin(), bytes.end());
    padded.resize(bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    const std::uint8_t* input = padded.data();
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        std::uint8_t* parsed{};
        int parsedSize{};
        const auto consumed = av_parser_parse2(m_parser.get(), m_codec.get(), &parsed, &parsedSize, input, remaining, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        CheckAv(consumed, "H.264 parse");
        if (parsedSize > 0) SendPacket(parsed, parsedSize);
        if (consumed == 0 && parsedSize == 0) throw std::runtime_error("H.264 parser made no progress");
        input += consumed;
        remaining -= consumed;
    }
}

// Sends one access unit to the decoder and collects what it finished; a full decoder is drained first.
void VideoDecoder::SendPacket(const std::uint8_t* data, int size)
{
    av_packet_unref(m_packet.get());
    CheckAv(av_new_packet(m_packet.get(), size), "Video packet allocation");
    std::copy_n(data, size, m_packet->data);
    auto result = avcodec_send_packet(m_codec.get(), m_packet.get());
    if (result == AVERROR(EAGAIN)) {
        Drain();
        result = avcodec_send_packet(m_codec.get(), m_packet.get());
    }
    CheckAv(result, "H.264 send packet");
    Drain();
}

// Takes every finished picture from the decoder, converts it to RGB888 and hands it to the frame callback.
void VideoDecoder::Drain()
{
    for (;;) {
        const auto result = avcodec_receive_frame(m_codec.get(), m_frame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
        CheckAv(result, "H.264 receive frame");
        if (m_frame->width <= 0 || m_frame->height <= 0 || m_frame->width > kMaxWidth || m_frame->height > kMaxHeight)
            throw std::runtime_error("Video dimensions exceed the PoC limit");
        m_scaler.reset(sws_getCachedContext(m_scaler.release(), m_frame->width, m_frame->height, static_cast<AVPixelFormat>(m_frame->format),
            m_frame->width, m_frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr));
        if (!m_scaler) throw std::runtime_error("Video colour converter allocation failed");
        VideoFrame output{m_frame->width, m_frame->height, m_frame->width * 3, {}};
        output.pixels.resize(static_cast<std::size_t>(output.stride) * output.height);
        std::uint8_t* destination[]{output.pixels.data()};
        const int strides[]{output.stride};
        CheckAv(sws_scale(m_scaler.get(), m_frame->data, m_frame->linesize, 0, m_frame->height, destination, strides), "Video colour conversion");
        m_onFrame(std::move(output));
        av_frame_unref(m_frame.get());
    }
}
}
