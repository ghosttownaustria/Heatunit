#include "usb/LibusbUsbBackend.h"
#include "usb/LibusbHandles.h"
#include "usb/UsbLogging.h"
#include <array>
#include <cstddef>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>

namespace headunit {
namespace {
// One string descriptor of the device: where it goes, and its name for messages and in sysfs.
struct StringItem {
    std::uint8_t index;
    std::string UsbDevice::*field;
    const char* name;
    const char* sysfsAttribute;
};

// "1-2.3": the bus and the chain of hub ports below it.
std::string PortPath(libusb_device* device)
{
    std::array<std::uint8_t, 8> ports{};
    const int count = libusb_get_port_numbers(device, ports.data(), static_cast<int>(ports.size()));
    std::string path = std::to_string(libusb_get_bus_number(device));
    if (count <= 0) return path;
    path += '-';
    for (int index = 0; index < count; ++index) {
        if (index > 0) path += '.';
        path += std::to_string(ports[static_cast<std::size_t>(index)]);
    }
    return path;
}

// The interfaces (every alternate setting) and endpoints of a configuration descriptor.
UsbConfiguration Convert(const libusb_config_descriptor& config)
{
    UsbConfiguration result;
    result.value = config.bConfigurationValue;
    for (int interfaceIndex = 0; interfaceIndex < config.bNumInterfaces; ++interfaceIndex) {
        const auto& group = config.interface[interfaceIndex];
        for (int alternateIndex = 0; alternateIndex < group.num_altsetting; ++alternateIndex) {
            const auto& descriptor = group.altsetting[alternateIndex];
            UsbInterface usbInterface;
            usbInterface.number = descriptor.bInterfaceNumber;
            usbInterface.alternateSetting = descriptor.bAlternateSetting;
            usbInterface.classCode = descriptor.bInterfaceClass;
            usbInterface.subclassCode = descriptor.bInterfaceSubClass;
            usbInterface.protocolCode = descriptor.bInterfaceProtocol;
            for (int endpointIndex = 0; endpointIndex < descriptor.bNumEndpoints; ++endpointIndex) {
                const auto& endpoint = descriptor.endpoint[endpointIndex];
                usbInterface.endpoints.push_back({endpoint.bEndpointAddress, endpoint.bmAttributes, endpoint.wMaxPacketSize, endpoint.bInterval});
            }
            result.interfaces.push_back(std::move(usbInterface));
        }
    }
    return result;
}

// A string the kernel read when it enumerated the device and publishes to everyone; nothing off Linux or when the
// attribute is missing.
std::optional<std::string> ReadSysfsString([[maybe_unused]] const std::string& node, [[maybe_unused]] const char* attribute)
{
#ifdef __linux__
    std::ifstream file("/sys/bus/usb/devices/" + node + "/" + attribute);
    std::string value;
    if (file && std::getline(file, value)) return value;
#endif
    return std::nullopt;
}

// Reads manufacturer, product and serial number: from sysfs where possible, otherwise from the device, which is opened
// once for that. What cannot be read gets a placeholder and a diagnostic line.
void ReadStrings(libusb_device* device, const std::string& node, const libusb_device_descriptor& descriptor, UsbDevice& result)
{
    const StringItem items[] = {
        {descriptor.iManufacturer, &UsbDevice::manufacturer, "Manufacturer", "manufacturer"},
        {descriptor.iProduct, &UsbDevice::product, "Product", "product"},
        {descriptor.iSerialNumber, &UsbDevice::serial, "Serial", "serial"},
    };
    LibusbHandle handle;
    bool hasTriedOpen = false;
    int openError = 0;
    for (const auto& item : items) {
        std::string& destination = result.*item.field;
        if (item.index == 0) {
            destination = kUsbStringNotSupplied;
            continue;
        }
        if (const auto value = ReadSysfsString(node, item.sysfsAttribute)) {
            destination = *value;
            continue;
        }
        if (!hasTriedOpen) {
            hasTriedOpen = true;
            libusb_device_handle* rawHandle = nullptr;
            openError = libusb_open(device, &rawHandle);
            if (openError == 0) handle.reset(rawHandle);
        }
        if (!handle) {
            destination = kUsbStringUnavailable;
            result.diagnostics.push_back(std::string(item.name) + ": the device cannot be opened to read it (" + LibusbErrorText(openError) + ")");
            continue;
        }
        unsigned char buffer[256]{};
        const int length = libusb_get_string_descriptor_ascii(handle.get(), item.index, buffer, sizeof(buffer));
        if (length < 0) {
            destination = kUsbStringUnavailable;
            result.diagnostics.push_back(std::string(item.name) + ": " + LibusbErrorText(length));
            continue;
        }
        destination.assign(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(length));
    }
}

// Reads every configuration descriptor of the device; one that cannot be read becomes a diagnostic line.
void ReadConfigurations(libusb_device* device, const libusb_device_descriptor& descriptor, UsbDevice& result)
{
    libusb_config_descriptor* rawActive = nullptr;
    // Fails for a device that is not configured; 0 then means "no active configuration".
    if (libusb_get_active_config_descriptor(device, &rawActive) == 0) {
        const LibusbConfig active(rawActive);
        result.activeConfiguration = active->bConfigurationValue;
    }
    for (unsigned index = 0; index < descriptor.bNumConfigurations; ++index) {
        libusb_config_descriptor* rawConfig = nullptr;
        if (const int code = libusb_get_config_descriptor(device, static_cast<std::uint8_t>(index), &rawConfig); code < 0) {
            result.diagnostics.push_back("Configuration " + std::to_string(index) + ": " + LibusbErrorText(code));
            continue;
        }
        const LibusbConfig config(rawConfig);
        result.configurations.push_back(Convert(*config));
    }
}

// The root hubs are the buses themselves; like the Windows backend, only what is attached to them is listed.
bool IsRootHub(libusb_device* device, const libusb_device_descriptor& descriptor)
{
    return descriptor.bDeviceClass == LIBUSB_CLASS_HUB && libusb_get_parent(device) == nullptr && libusb_get_port_number(device) == 0;
}
}

// A backend that logs every device and a summary, unless `isQuiet` (for code that polls).
LibusbUsbBackend::LibusbUsbBackend(Logger& logger, bool isQuiet) : m_logger(logger), m_isQuiet(isQuiet)
{
}

// Lists every device on the bus except the root hubs; a device whose descriptor cannot be read becomes a scan error.
UsbScanResult LibusbUsbBackend::EnumerateDevices()
{
    UsbScanResult result;
    if (!m_isQuiet) m_logger.Write(LogLevel::Info, "USB", "Scanning USB devices through libusb (descriptor reads only)");
    libusb_context* rawContext = nullptr;
    if (const int code = libusb_init(&rawContext); code < 0) throw std::runtime_error("libusb_init: " + LibusbErrorText(code));
    const LibusbContext context(rawContext);
    libusb_device** rawList = nullptr;
    const auto count = libusb_get_device_list(context.get(), &rawList);
    if (count < 0) throw std::runtime_error("libusb_get_device_list: " + LibusbErrorText(static_cast<int>(count)));
    const LibusbDeviceList list(rawList);
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        libusb_device* device = rawList[index];
        const std::string node = PortPath(device);
        libusb_device_descriptor descriptor{};
        if (const int code = libusb_get_device_descriptor(device, &descriptor); code < 0) {
            result.errors.push_back("Read device descriptor of usb:" + node + ": " + LibusbErrorText(code));
            continue;
        }
        if (IsRootHub(device, descriptor)) continue;
        UsbDevice usb;
        usb.location = "usb:" + node;
        usb.vendorId = descriptor.idVendor;
        usb.productId = descriptor.idProduct;
        ReadStrings(device, node, descriptor, usb);
        ReadConfigurations(device, descriptor, usb);
        if (!m_isQuiet) LogUsbDevice(m_logger, usb);
        result.devices.push_back(std::move(usb));
    }
    if (!m_isQuiet) LogUsbScanSummary(m_logger, result);
    return result;
}

// The `location` libusb devices get: "usb:<bus>-<port>.<port>...", the way Linux names USB devices in sysfs (usb:1-2.3
// is /sys/bus/usb/devices/1-2.3). It is stable while the phone stays on its port.
std::string LibusbLocation(libusb_device* device)
{
    return "usb:" + PortPath(device);
}
}
