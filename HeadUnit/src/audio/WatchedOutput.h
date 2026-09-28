#pragma once
#include "audio/IPcmOutput.h"
#include "audio/MediaActivity.h"
#include <memory>

namespace headunit {
// The phone's media output, telling a MediaActivity about every block the phone plays.
class WatchedOutput final : public IPcmOutput {
public:
    WatchedOutput(std::shared_ptr<IPcmOutput> output, std::shared_ptr<MediaActivity> activity);

    void Write(std::span<const std::uint8_t> pcm) override;
    void Flush() override;
    std::size_t Queued() const override;

private:
    std::shared_ptr<IPcmOutput> m_output;
    std::shared_ptr<MediaActivity> m_activity;
};
}
