#include "video/FfmpegHandles.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace headunit {
// Frees the decoder context.
void AvCodecContextDeleter::operator()(AVCodecContext* context) const
{
    avcodec_free_context(&context);
}

// Closes the parser.
void AvParserDeleter::operator()(AVCodecParserContext* parser) const
{
    av_parser_close(parser);
}

// Closes the input and frees its context.
void AvFormatContextDeleter::operator()(AVFormatContext* context) const
{
    avformat_close_input(&context);
}

// Frees the frame.
void AvFrameDeleter::operator()(AVFrame* frame) const
{
    av_frame_free(&frame);
}

// Frees the packet.
void AvPacketDeleter::operator()(AVPacket* packet) const
{
    av_packet_free(&packet);
}

// Frees the resampler.
void SwrContextDeleter::operator()(SwrContext* resampler) const
{
    swr_free(&resampler);
}

// Frees the scaler.
void SwsContextDeleter::operator()(SwsContext* scaler) const
{
    sws_freeContext(scaler);
}

// FFmpeg's text for an error code.
std::string AvErrorText(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return text;
}
}
