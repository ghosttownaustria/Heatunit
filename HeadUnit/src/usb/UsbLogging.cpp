#include "usb/UsbLogging.h"
#include "androidauto/AndroidDeviceDetector.h"
#include <iomanip>
#include <sstream>

namespace headunit {
std::string UsbHex(unsigned value, int width)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}
void LogUsbDevice(Logger& logger, const UsbDevice& device)
{
    logger.Write("INFO", "USB", "Device detected at " + device.location);
    logger.Write("DEBUG", "USB", "VID=" + UsbHex(device.vendorId) + " PID=" + UsbHex(device.productId) + " activeConfiguration=" + std::to_string(device.activeConfiguration));
    logger.Write("DEBUG", "USB", "Manufacturer=" + device.manufacturer + " Product=" + device.product + " Serial=" + device.serial);
    for (const auto& config : device.configurations) {
        logger.Write("DEBUG", "USB", "Configuration=" + std::to_string(config.value) + " interface descriptors=" + std::to_string(config.interfaces.size()));
        for (const auto& interface : config.interfaces) {
            logger.Write("DEBUG", "USB", "Interface=" + std::to_string(interface.number) + " alt=" + std::to_string(interface.alternateSetting) +
                " class/subclass/protocol=" + UsbHex(interface.classCode, 2) + "/" + UsbHex(interface.subclassCode, 2) + "/" + UsbHex(interface.protocolCode, 2));
            for (const auto& endpoint : interface.endpoints)
                logger.Write("DEBUG", "USB", "Endpoint=0x" + UsbHex(endpoint.address, 2) + ((endpoint.address & 0x80) ? " IN" : " OUT") +
                    " type=" + std::to_string(endpoint.attributes & 3) + " maxPacket=" + std::to_string(endpoint.maxPacketSize) + " interval=" + std::to_string(endpoint.interval));
        }
    }
    for (const auto& diagnostic : device.diagnostics) logger.Write("WARN", "USB", diagnostic);
    const auto detection = DetectAndroidDevice(device);
    logger.Write("INFO", "ANDROID", detection.reason);
    if (detection.evidence == AndroidEvidence::AccessoryMode)
        logger.Write(detection.hasAccessoryBulkPair ? "INFO" : "WARN", "USB", detection.hasAccessoryBulkPair ?
            "Accessory interface has bulk IN/OUT descriptors; transport access not tested" : "Accessory bulk endpoint pair unavailable in active configuration");
}
}
