#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <vector>

namespace headunit {
struct VideoFrame {
    int width{}, height{}, stride{};
    std::vector<std::uint8_t> pixels;
};
class VideoDecoder {
public:
    explicit VideoDecoder(std::function<void(VideoFrame)> onFrame);
    ~VideoDecoder();
    void Decode(std::span<const std::uint8_t> bytes);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
}
