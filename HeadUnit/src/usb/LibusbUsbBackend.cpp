#include "usb/LibusbUsbBackend.h"
#include "usb/UsbLogging.h"
#include <libusb.h>
#include <array>
#include <cstddef>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>

namespace headunit {
namespace {
struct ContextDeleter { void operator()(libusb_context* context) const { libusb_exit(context); } };
struct ListDeleter { void operator()(libusb_device** devices) const { libusb_free_device_list(devices, 1); } };
struct HandleDeleter { void operator()(libusb_device_handle* handle) const { libusb_close(handle); } };
struct ConfigDeleter { void operator()(libusb_config_descriptor* config) const { libusb_free_config_descriptor(config); } };

std::string Error(int code)
{
    return std::string(libusb_error_name(code)) + " (" + std::to_string(code) + ")";
}
// "1-2.3": the bus and the chain of hub ports below it.
std::string PortPath(libusb_device* device)
{
    std::array<std::uint8_t, 8> ports{};
    const int count = libusb_get_port_numbers(device, ports.data(), static_cast<int>(ports.size()));
    std::string path = std::to_string(libusb_get_bus_number(device));
    if (count <= 0) return path;
    path += '-';
    for (int i = 0; i < count; ++i) {
        if (i > 0) path += '.';
        path += std::to_string(ports[static_cast<std::size_t>(i)]);
    }
    return path;
}
UsbConfiguration Convert(const libusb_config_descriptor& config)
{
    UsbConfiguration result;
    result.value = config.bConfigurationValue;
    for (int i = 0; i < config.bNumInterfaces; ++i) {
        const auto& group = config.interface[i];
        for (int alt = 0; alt < group.num_altsetting; ++alt) {
            const auto& descriptor = group.altsetting[alt];
            UsbInterface iface;
            iface.number = descriptor.bInterfaceNumber;
            iface.alternateSetting = descriptor.bAlternateSetting;
            iface.classCode = descriptor.bInterfaceClass;
            iface.subclassCode = descriptor.bInterfaceSubClass;
            iface.protocolCode = descriptor.bInterfaceProtocol;
            for (int ep = 0; ep < descriptor.bNumEndpoints; ++ep) {
                const auto& endpoint = descriptor.endpoint[ep];
                iface.endpoints.push_back({endpoint.bEndpointAddress, endpoint.bmAttributes, endpoint.wMaxPacketSize, endpoint.bInterval});
            }
            result.interfaces.push_back(std::move(iface));
        }
    }
    return result;
}
#ifdef __linux__
// The kernel has read the device's strings when it enumerated it and publishes them to everyone.
std::optional<std::string> ReadSysfsString(const std::string& node, const char* attribute)
{
    std::ifstream file("/sys/bus/usb/devices/" + node + "/" + attribute);
    std::string value;
    if (!file || !std::getline(file, value)) return std::nullopt;
    return value;
}
#endif
void ReadStrings(libusb_device* device, [[maybe_unused]] const std::string& node, const libusb_device_descriptor& descriptor, UsbDevice& result)
{
    struct Item { std::uint8_t index; std::string UsbDevice::* field; const char* name; [[maybe_unused]] const char* attribute; };
    const Item items[] = {
        {descriptor.iManufacturer, &UsbDevice::manufacturer, "Manufacturer", "manufacturer"},
        {descriptor.iProduct, &UsbDevice::product, "Product", "product"},
        {descriptor.iSerialNumber, &UsbDevice::serial, "Serial", "serial"},
    };
    std::unique_ptr<libusb_device_handle, HandleDeleter> handle;
    bool hasTriedOpen = false;
    int openError = 0;
    for (const auto& item : items) {
        std::string& destination = result.*item.field;
        if (item.index == 0) { destination = "<not supplied>"; continue; }
#ifdef __linux__
        if (const auto value = ReadSysfsString(node, item.attribute)) { destination = *value; continue; }
#endif
        if (!hasTriedOpen) {
            hasTriedOpen = true;
            libusb_device_handle* raw = nullptr;
            openError = libusb_open(device, &raw);
            if (openError == 0) handle.reset(raw);
        }
        if (!handle) {
            destination = "<unavailable>";
            result.diagnostics.push_back(std::string(item.name) + ": the device cannot be opened to read it (" + Error(openError) + ")");
            continue;
        }
        unsigned char buffer[256]{};
        const int length = libusb_get_string_descriptor_ascii(handle.get(), item.index, buffer, sizeof(buffer));
        if (length < 0) {
            destination = "<unavailable>";
            result.diagnostics.push_back(std::string(item.name) + ": " + Error(length));
            continue;
        }
        destination.assign(reinterpret_cast<const char*>(buffer), static_cast<std::size_t>(length));
    }
}
}

std::string LibusbLocation(libusb_device* device) { return "usb:" + PortPath(device); }

UsbScanResult LibusbUsbBackend::EnumerateDevices()
{
    UsbScanResult result;
    if (!m_isQuiet) m_logger.Write("INFO", "USB", "Scanning USB devices through libusb (descriptor reads only)");
    libusb_context* rawContext = nullptr;
    if (const int code = libusb_init(&rawContext); code < 0) throw std::runtime_error("libusb_init: " + Error(code));
    const std::unique_ptr<libusb_context, ContextDeleter> context(rawContext);
    libusb_device** rawList = nullptr;
    const auto count = libusb_get_device_list(context.get(), &rawList);
    if (count < 0) throw std::runtime_error("libusb_get_device_list: " + Error(static_cast<int>(count)));
    const std::unique_ptr<libusb_device*, ListDeleter> list(rawList);
    for (std::ptrdiff_t index = 0; index < count; ++index) {
        libusb_device* device = rawList[index];
        const std::string node = PortPath(device);
        libusb_device_descriptor descriptor{};
        if (const int code = libusb_get_device_descriptor(device, &descriptor); code < 0) {
            result.errors.push_back("Read device descriptor of usb:" + node + ": " + Error(code));
            continue;
        }
        // The root hubs are the buses themselves; like the Windows backend, list what is attached to them.
        if (descriptor.bDeviceClass == LIBUSB_CLASS_HUB && libusb_get_parent(device) == nullptr && libusb_get_port_number(device) == 0) continue;
        UsbDevice usb;
        usb.location = "usb:" + node;
        usb.vendorId = descriptor.idVendor;
        usb.productId = descriptor.idProduct;
        libusb_config_descriptor* rawActive = nullptr;
        // Fails for a device that is not configured; 0 then means "no active configuration".
        if (libusb_get_active_config_descriptor(device, &rawActive) == 0) {
            const std::unique_ptr<libusb_config_descriptor, ConfigDeleter> active(rawActive);
            usb.activeConfiguration = active->bConfigurationValue;
        }
        ReadStrings(device, node, descriptor, usb);
        for (unsigned config = 0; config < descriptor.bNumConfigurations; ++config) {
            libusb_config_descriptor* raw = nullptr;
            if (const int code = libusb_get_config_descriptor(device, static_cast<std::uint8_t>(config), &raw); code < 0) {
                usb.diagnostics.push_back("Configuration " + std::to_string(config) + ": " + Error(code));
                continue;
            }
            const std::unique_ptr<libusb_config_descriptor, ConfigDeleter> owned(raw);
            usb.configurations.push_back(Convert(*owned));
        }
        if (!m_isQuiet) LogUsbDevice(m_logger, usb);
        result.devices.push_back(std::move(usb));
    }
    if (!m_isQuiet) {
        for (const auto& error : result.errors) m_logger.Write("ERROR", "USB", error);
        m_logger.Write("INFO", "USB", "Scan finished: devices=" + std::to_string(result.devices.size()) + " scan errors=" + std::to_string(result.errors.size()));
        m_logger.Write("INFO", "AA", "Discovery finished; scanning alone does not start an Android Auto session");
    }
    return result;
}
}
