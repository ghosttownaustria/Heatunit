#include "video/VideoDecoder.h"
#include <stdexcept>
#include <string>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

namespace headunit {
namespace {
void CheckAv(int code, const char* operation) {
    if (code >= 0) return;
    char description[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, description, sizeof(description));
    throw std::runtime_error(std::string(operation) + ": " + description);
}
}
struct VideoDecoder::Impl {
    AVCodecContext* codec{};
    AVCodecParserContext* parser{};
    AVFrame* frame{};
    AVPacket* packet{};
    SwsContext* scaler{};
    std::function<void(VideoFrame)> onFrame;
    ~Impl() {
        sws_freeContext(scaler);
        av_packet_free(&packet);
        av_frame_free(&frame);
        av_parser_close(parser);
        avcodec_free_context(&codec);
    }
    void Drain() {
        for (;;) {
            const auto result = avcodec_receive_frame(codec, frame);
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
            CheckAv(result, "H.264 receive frame");
            if (frame->width <= 0 || frame->height <= 0 || frame->width > 1920 || frame->height > 1080)
                throw std::runtime_error("Video dimensions exceed the PoC limit");
            scaler = sws_getCachedContext(scaler, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                frame->width, frame->height, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler) throw std::runtime_error("Video colour converter allocation failed");
            VideoFrame output{frame->width, frame->height, frame->width * 3, {}};
            output.pixels.resize(static_cast<std::size_t>(output.stride) * output.height);
            std::uint8_t* destination[]{output.pixels.data()};
            const int strides[]{output.stride};
            CheckAv(sws_scale(scaler, frame->data, frame->linesize, 0, frame->height, destination, strides), "Video colour conversion");
            onFrame(std::move(output));
            av_frame_unref(frame);
        }
    }
};
VideoDecoder::VideoDecoder(std::function<void(VideoFrame)> onFrame) : m_impl(std::make_unique<Impl>()) {
    m_impl->onFrame = std::move(onFrame);
    const auto* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) throw std::runtime_error("FFmpeg H.264 decoder unavailable");
    m_impl->codec = avcodec_alloc_context3(codec);
    m_impl->parser = av_parser_init(AV_CODEC_ID_H264);
    m_impl->frame = av_frame_alloc();
    m_impl->packet = av_packet_alloc();
    if (!m_impl->codec || !m_impl->parser || !m_impl->frame || !m_impl->packet)
        throw std::runtime_error("H.264 decoder allocation failed");
    m_impl->codec->thread_count = 2;
    m_impl->codec->thread_type = FF_THREAD_SLICE;
    CheckAv(avcodec_open2(m_impl->codec, codec, nullptr), "H.264 decoder open");
}
VideoDecoder::~VideoDecoder() = default;
void VideoDecoder::Decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 4 * 1024 * 1024) throw std::runtime_error("Video packet exceeds limit");
    std::vector<std::uint8_t> padded(bytes.begin(), bytes.end());
    padded.resize(bytes.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    auto* input = padded.data();
    int remaining = static_cast<int>(bytes.size());
    while (remaining > 0) {
        std::uint8_t* parsed{};
        int parsedSize{};
        const auto consumed = av_parser_parse2(m_impl->parser, m_impl->codec, &parsed, &parsedSize,
            input, remaining, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
        CheckAv(consumed, "H.264 parse");
        if (parsedSize > 0) {
            av_packet_unref(m_impl->packet);
            CheckAv(av_new_packet(m_impl->packet, parsedSize), "Video packet allocation");
            std::copy_n(parsed, parsedSize, m_impl->packet->data);
            auto result = avcodec_send_packet(m_impl->codec, m_impl->packet);
            if (result == AVERROR(EAGAIN)) { m_impl->Drain(); result = avcodec_send_packet(m_impl->codec, m_impl->packet); }
            CheckAv(result, "H.264 send packet");
            m_impl->Drain();
        }
        if (consumed == 0 && parsedSize == 0) throw std::runtime_error("H.264 parser made no progress");
        input += consumed;
        remaining -= consumed;
    }
}
}
