#pragma once
#include "video/FfmpegHandles.h"
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

namespace headunit {
// One decoded picture as RGB888.
struct VideoFrame {
    int width{}, height{}, stride{};
    std::vector<std::uint8_t> pixels;
};

// Decodes the phone's H.264 stream (FFmpeg) into RGB888 frames. Packets may hold partial or several access units; the
// parser splits them. Throws on anything it cannot decode; the caller decides whether to go on.
class VideoDecoder {
public:
    explicit VideoDecoder(std::function<void(VideoFrame)> onFrame);

    void Decode(std::span<const std::uint8_t> bytes);

private:
    std::function<void(VideoFrame)> m_onFrame;
    AvCodecContextPtr m_codec;
    AvParserPtr m_parser;
    AvFramePtr m_frame;
    AvPacketPtr m_packet;
    SwsContextPtr m_scaler;

    void SendPacket(const std::uint8_t* data, int size);
    void Drain();
};
}
