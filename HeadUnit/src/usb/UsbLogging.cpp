#include "usb/UsbLogging.h"
#include "androidauto/AndroidDeviceDetector.h"
#include <iomanip>
#include <sstream>

namespace headunit {
namespace {
// The descriptor lines of one configuration: its interfaces and their endpoints.
void LogConfiguration(Logger& logger, const UsbConfiguration& configuration)
{
    logger.Write(LogLevel::Debug, "USB", "Configuration=" + std::to_string(configuration.value) + " interface descriptors=" +
        std::to_string(configuration.interfaces.size()));
    for (const auto& usbInterface : configuration.interfaces) {
        logger.Write(LogLevel::Debug, "USB", "Interface=" + std::to_string(usbInterface.number) + " alt=" + std::to_string(usbInterface.alternateSetting) +
            " class/subclass/protocol=" + UsbHex(usbInterface.classCode, 2) + "/" + UsbHex(usbInterface.subclassCode, 2) + "/" +
            UsbHex(usbInterface.protocolCode, 2));
        for (const auto& endpoint : usbInterface.endpoints) {
            logger.Write(LogLevel::Debug, "USB", "Endpoint=0x" + UsbHex(endpoint.address, 2) + ((endpoint.address & 0x80) ? " IN" : " OUT") +
                " type=" + std::to_string(endpoint.attributes & 3) + " maxPacket=" + std::to_string(endpoint.maxPacketSize) +
                " interval=" + std::to_string(endpoint.interval));
        }
    }
}
}

// Upper-case hexadecimal, zero padded to `width` digits.
std::string UsbHex(unsigned value, int width)
{
    std::ostringstream stream;
    stream << std::uppercase << std::hex << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

// "04E8:6860": vendor and product id as the USB world writes them.
std::string UsbIdText(std::uint16_t vendorId, std::uint16_t productId)
{
    return UsbHex(vendorId) + ":" + UsbHex(productId);
}

// The lines every discovery backend writes for a detected device: identity, strings, descriptors and what the
// Android detector makes of it.
void LogUsbDevice(Logger& logger, const UsbDevice& device)
{
    logger.Write(LogLevel::Info, "USB", "Device detected at " + device.location);
    logger.Write(LogLevel::Debug, "USB", "VID=" + UsbHex(device.vendorId) + " PID=" + UsbHex(device.productId) + " activeConfiguration=" +
        std::to_string(device.activeConfiguration));
    logger.Write(LogLevel::Debug, "USB", "Manufacturer=" + device.manufacturer + " Product=" + device.product + " Serial=" + device.serial);
    for (const auto& configuration : device.configurations) LogConfiguration(logger, configuration);
    for (const auto& diagnostic : device.diagnostics) logger.Write(LogLevel::Warning, "USB", diagnostic);
    const auto detection = DetectAndroidDevice(device);
    logger.Write(LogLevel::Info, "ANDROID", detection.reason);
    if (detection.evidence != AndroidEvidence::AccessoryMode) return;
    if (detection.hasAccessoryBulkPair)
        logger.Write(LogLevel::Info, "USB", "Accessory interface has bulk IN/OUT descriptors; transport access not tested");
    else
        logger.Write(LogLevel::Warning, "USB", "Accessory bulk endpoint pair unavailable in active configuration");
}

// The closing lines of a scan that every discovery backend writes: its errors and the counts.
void LogUsbScanSummary(Logger& logger, const UsbScanResult& result)
{
    for (const auto& error : result.errors) logger.Write(LogLevel::Error, "USB", error);
    logger.Write(LogLevel::Info, "USB", "Scan finished: devices=" + std::to_string(result.devices.size()) + " scan errors=" +
        std::to_string(result.errors.size()));
    logger.Write(LogLevel::Info, "AA", "Discovery finished; scanning alone does not start an Android Auto session");
}
}
