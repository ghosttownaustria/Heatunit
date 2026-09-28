#pragma once
#include <memory>
#include <string>

struct AVCodecContext;
struct AVCodecParserContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwrContext;
struct SwsContext;

namespace headunit {
// Frees a decoder context.
struct AvCodecContextDeleter {
    void operator()(AVCodecContext* context) const;
};

// Closes a bitstream parser.
struct AvParserDeleter {
    void operator()(AVCodecParserContext* parser) const;
};

// Closes an input (and frees its context, opened or not).
struct AvFormatContextDeleter {
    void operator()(AVFormatContext* context) const;
};

// Frees a frame.
struct AvFrameDeleter {
    void operator()(AVFrame* frame) const;
};

// Frees a packet.
struct AvPacketDeleter {
    void operator()(AVPacket* packet) const;
};

// Frees an audio resampler.
struct SwrContextDeleter {
    void operator()(SwrContext* resampler) const;
};

// Frees a picture scaler.
struct SwsContextDeleter {
    void operator()(SwsContext* scaler) const;
};

using AvCodecContextPtr = std::unique_ptr<AVCodecContext, AvCodecContextDeleter>;
using AvParserPtr = std::unique_ptr<AVCodecParserContext, AvParserDeleter>;
using AvFormatContextPtr = std::unique_ptr<AVFormatContext, AvFormatContextDeleter>;
using AvFramePtr = std::unique_ptr<AVFrame, AvFrameDeleter>;
using AvPacketPtr = std::unique_ptr<AVPacket, AvPacketDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, SwrContextDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;

std::string AvErrorText(int code);
}
