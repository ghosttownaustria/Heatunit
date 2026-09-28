#include "CoreTestSuites.h"
#include "TestSupport.h"
#include "androidauto/AoaNegotiator.h"
#include <stdexcept>
#include <vector>

namespace {
// A phone's control endpoint that checks the AOA requests it gets and fails at a chosen one.
class RecordingControl final : public headunit::IUsbControl {
public:
    int m_callCount{};
    int m_failAt{-1};
    bool m_isShortTransfer{}, m_isDisconnected{}, m_hasStarted{};
    // Checks the request and answers it as the phone would, failing where the test asks.
    headunit::UsbControlResult Transfer(std::uint8_t requestType, std::uint8_t request,
        std::uint16_t index, std::span<std::uint8_t> bytes) override
    {
        const auto call = m_callCount++;
        if (requestType != 0x40) throw std::runtime_error("Wrong control transfer direction/type");
        if (request == 53) {
            if (call != 6 || index != 0 || !bytes.empty()) throw std::runtime_error("START sent before full identity");
            m_hasStarted = true;
            return m_isDisconnected ? headunit::UsbControlResult{-4, "disconnected", true} : headunit::UsbControlResult{};
        }
        if (request != 52 || index != call || bytes.empty() || bytes.back() != 0 || bytes.size() > 256)
            throw std::runtime_error("Invalid AOA identity request");
        if (index == 3 && bytes.size() < 2) throw std::runtime_error("Version must not be empty");
        if (call == m_failAt) {
            if (m_isShortTransfer) return {static_cast<int>(bytes.size()) - 1, "", false};
            return {-9, "pipe", false};
        }
        return {static_cast<int>(bytes.size()), "", false};
    }
};
}

// The AOA identity requests go out in order and START only after all of them; a failed or short request stops before START.
void RunAoaTests()
{
    std::vector<std::string> stages;
    const auto report = [&](const std::string& stage) { stages.push_back(stage); };
    RecordingControl success;
    headunit::RequestAccessoryMode(success, report);
    Check(success.m_hasStarted && success.m_callCount == 7 && stages.size() == 7, "Incomplete AOA sequence");
    for (int failAt = 0; failAt < 6; ++failAt) {
        for (const bool isShort : {false, true}) {
            RecordingControl failure;
            failure.m_failAt = failAt;
            failure.m_isShortTransfer = isShort;
            bool hasRejected = false;
            try { headunit::RequestAccessoryMode(failure, report); }
            catch (const std::runtime_error&) { hasRejected = true; }
            Check(hasRejected && !failure.m_hasStarted && failure.m_callCount == failAt + 1, "AOA failure did not stop before START");
        }
    }
    RecordingControl disconnect;
    disconnect.m_isDisconnected = true;
    headunit::RequestAccessoryMode(disconnect, report);
    Check(disconnect.m_hasStarted, "START disconnect handling failed");
}
