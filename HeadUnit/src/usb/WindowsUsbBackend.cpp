#include "usb/WindowsUsbBackend.h"
#include "platform/WindowsSupport.h"
#include "usb/UsbDescriptors.h"
#include "usb/UsbLogging.h"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <stdexcept>
#include <utility>
#include <vector>

namespace headunit {
namespace {
constexpr USHORT kEnglishLanguage = 0x0409;

// Reads one descriptor of the device at `port` of `hub` through the hub (no driver of the device is involved).
std::vector<std::uint8_t> ReadDescriptor(HANDLE hub, ULONG port, UCHAR type, UCHAR index, USHORT language, USHORT size)
{
    std::vector<std::uint8_t> buffer(sizeof(USB_DESCRIPTOR_REQUEST) + size);
    auto* request = reinterpret_cast<USB_DESCRIPTOR_REQUEST*>(buffer.data());
    request->ConnectionIndex = port;
    request->SetupPacket.bmRequest = 0x80;
    request->SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
    request->SetupPacket.wValue = static_cast<USHORT>((type << 8) | index);
    request->SetupPacket.wIndex = language;
    request->SetupPacket.wLength = size;
    DWORD returned = 0;
    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, request, static_cast<DWORD>(buffer.size()), request,
            static_cast<DWORD>(buffer.size()), &returned, nullptr))
        throw std::runtime_error(WindowsErrorText("Get descriptor type=" + std::to_string(type)));
    if (returned < sizeof(USB_DESCRIPTOR_REQUEST) || returned > buffer.size()) throw std::runtime_error("Invalid descriptor response size");
    return {buffer.begin() + sizeof(USB_DESCRIPTOR_REQUEST), buffer.begin() + returned};
}

// One string descriptor as UTF-8; the placeholder for index 0 (the device supplies none).
std::string ReadString(HANDLE hub, ULONG port, UCHAR index, USHORT language)
{
    if (index == 0) return kUsbStringNotSupplied;
    const auto bytes = ReadDescriptor(hub, port, USB_STRING_DESCRIPTOR_TYPE, index, language, 255);
    if (bytes.size() < 2 || bytes[1] != USB_STRING_DESCRIPTOR_TYPE || bytes[0] < 2 || bytes[0] > bytes.size() || bytes[0] % 2 != 0)
        throw std::runtime_error("Malformed USB string descriptor");
    std::wstring value;
    for (std::size_t offset = 2; offset < bytes[0]; offset += 2) value.push_back(static_cast<wchar_t>(bytes[offset] | (bytes[offset + 1] << 8)));
    return WideToUtf8(value);
}

// The device's first string language, English when it has none that can be read.
USHORT ReadLanguage(HANDLE hub, ULONG port, const USB_DEVICE_DESCRIPTOR& descriptor, UsbDevice& device)
{
    if (!descriptor.iManufacturer && !descriptor.iProduct && !descriptor.iSerialNumber) return kEnglishLanguage;
    try {
        const auto languages = ReadDescriptor(hub, port, USB_STRING_DESCRIPTOR_TYPE, 0, 0, 255);
        if (languages.size() < 4 || languages[1] != 3 || languages[0] < 4 || languages[0] > languages.size() || languages[0] % 2 != 0)
            throw std::runtime_error("Malformed USB language descriptor");
        return static_cast<USHORT>(languages[2] | (languages[3] << 8));
    } catch (const std::exception& error) {
        device.diagnostics.push_back(std::string(error.what()) + "; trying language 0409");
        return kEnglishLanguage;
    }
}

// Reads the strings and configurations of the device at `port`; what cannot be read becomes a diagnostic line.
void ReadDetails(HANDLE hub, ULONG port, const USB_DEVICE_DESCRIPTOR& descriptor, UsbDevice& device)
{
    const USHORT language = ReadLanguage(hub, port, descriptor, device);
    const auto readString = [&](UCHAR index, std::string& destination, const char* name) {
        try {
            destination = ReadString(hub, port, index, language);
        } catch (const std::exception& error) {
            destination = kUsbStringUnavailable;
            device.diagnostics.push_back(std::string(name) + ": " + error.what());
        }
    };
    readString(descriptor.iManufacturer, device.manufacturer, "Manufacturer");
    readString(descriptor.iProduct, device.product, "Product");
    readString(descriptor.iSerialNumber, device.serial, "Serial");
    for (unsigned index = 0; index < descriptor.bNumConfigurations; ++index) {
        try {
            const auto header = ReadDescriptor(hub, port, USB_CONFIGURATION_DESCRIPTOR_TYPE, static_cast<UCHAR>(index), 0, 9);
            if (header.size() < 9) throw std::runtime_error("Truncated configuration header");
            const auto length = static_cast<USHORT>(header[2] | (header[3] << 8));
            if (length < 9) throw std::runtime_error("Invalid configuration total length");
            device.configurations.push_back(ParseConfiguration(ReadDescriptor(hub, port, USB_CONFIGURATION_DESCRIPTOR_TYPE, static_cast<UCHAR>(index), 0, length)));
        } catch (const std::exception& error) {
            device.diagnostics.push_back("Configuration " + std::to_string(index) + ": " + error.what());
        }
    }
}

// The device at `port` of the hub, if one is connected there; problems go to `result.errors`.
void ScanPort(HANDLE hub, const std::string& hubPath, ULONG port, Logger& logger, UsbScanResult& result)
{
    // Space for all possible endpoint pipe records in the variable-sized response.
    std::vector<std::uint8_t> connectionBytes(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO));
    auto* connection = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connectionBytes.data());
    connection->ConnectionIndex = port;
    DWORD returned = 0;
    if (!DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, connection, static_cast<DWORD>(connectionBytes.size()), connection,
            static_cast<DWORD>(connectionBytes.size()), &returned, nullptr)) {
        result.errors.push_back(WindowsErrorText("Read connection " + hubPath + " port=" + std::to_string(port)));
        return;
    }
    if (connection->ConnectionStatus == NoDeviceConnected) return;
    if (connection->ConnectionStatus != DeviceConnected) {
        result.errors.push_back("Hub=" + hubPath + " port=" + std::to_string(port) + " connection status=" + std::to_string(connection->ConnectionStatus));
        return;
    }
    UsbDevice device;
    device.location = hubPath + "/port-" + std::to_string(port);
    device.vendorId = connection->DeviceDescriptor.idVendor;
    device.productId = connection->DeviceDescriptor.idProduct;
    device.activeConfiguration = connection->CurrentConfigurationValue;
    ReadDetails(hub, port, connection->DeviceDescriptor, device);
    LogUsbDevice(logger, device);
    result.devices.push_back(std::move(device));
}

// Every port of one hub; problems go to `result.errors`.
void ScanHub(HDEVINFO hubs, SP_DEVICE_INTERFACE_DATA& interfaceData, Logger& logger, UsbScanResult& result)
{
    DWORD size = 0;
    SetupDiGetDeviceInterfaceDetailW(hubs, &interfaceData, nullptr, 0, &size, nullptr);
    if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) {
        result.errors.push_back(WindowsErrorText("Get hub path size"));
        return;
    }
    std::vector<std::uint8_t> storage(size);
    auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
    detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
    if (!SetupDiGetDeviceInterfaceDetailW(hubs, &interfaceData, detail, size, nullptr, nullptr)) {
        result.errors.push_back(WindowsErrorText("Get hub path"));
        return;
    }
    const std::string hubPath = WideToUtf8(detail->DevicePath);
    const WindowsHandle hub(CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
    if (hub.get() == INVALID_HANDLE_VALUE) {
        result.errors.push_back(WindowsErrorText("Open hub " + hubPath));
        return;
    }
    USB_NODE_INFORMATION node{};
    node.NodeType = UsbHub;
    DWORD returned = 0;
    if (!DeviceIoControl(hub.get(), IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node), &node, sizeof(node), &returned, nullptr)) {
        result.errors.push_back(WindowsErrorText("Read hub " + hubPath));
        return;
    }
    const auto portCount = node.u.HubInformation.HubDescriptor.bNumberOfPorts;
    logger.Write(LogLevel::Debug, "USB", "Hub=" + hubPath + " ports=" + std::to_string(portCount));
    for (ULONG port = 1; port <= portCount; ++port) ScanPort(hub.get(), hubPath, port, logger, result);
}
}

// A backend that logs every device and a summary.
WindowsUsbBackend::WindowsUsbBackend(Logger& logger) : m_logger(logger)
{
}

// Asks every present USB hub for the devices on its ports.
UsbScanResult WindowsUsbBackend::EnumerateDevices()
{
    UsbScanResult result;
    m_logger.Write(LogLevel::Info, "USB", "Scanning present USB hubs (read-only descriptor requests)");
    const DeviceInfoSet hubs(SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE));
    if (hubs.get() == INVALID_HANDLE_VALUE) throw std::runtime_error(WindowsErrorText("SetupDiGetClassDevs"));
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(hubs.get(), nullptr, &GUID_DEVINTERFACE_USB_HUB, index, &interfaceData)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) result.errors.push_back(WindowsErrorText("SetupDiEnumDeviceInterfaces"));
            break;
        }
        ScanHub(hubs.get(), interfaceData, m_logger, result);
    }
    LogUsbScanSummary(m_logger, result);
    return result;
}
}
