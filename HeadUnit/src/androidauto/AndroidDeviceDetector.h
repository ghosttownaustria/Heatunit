#pragma once
#include "usb/UsbTypes.h"

namespace headunit {
enum class AndroidEvidence { None, Candidate, AdbInterface, AccessoryMode };
struct AndroidDetection {
    AndroidEvidence evidence{AndroidEvidence::None};
    std::string reason;
    bool hasAccessoryBulkPair{};
};
AndroidDetection DetectAndroidDevice(const UsbDevice& device);
}
