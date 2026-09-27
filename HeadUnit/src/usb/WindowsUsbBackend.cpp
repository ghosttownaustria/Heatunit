#include "usb/WindowsUsbBackend.h"
#include "usb/UsbDescriptors.h"
#include "usb/UsbLogging.h"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <initguid.h>
#include <usbiodef.h>
#include <usbioctl.h>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace headunit {
namespace {
struct HandleCloser { void operator()(void* handle) const { if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); } };
using Handle = std::unique_ptr<void, HandleCloser>;
struct DeviceSetCloser { void operator()(void* handle) const { SetupDiDestroyDeviceInfoList(handle); } };
using DeviceSet = std::unique_ptr<void, DeviceSetCloser>;

std::string Utf8(const std::wstring& value)
{
    if (value.empty()) return {};
    const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr);
    return result;
}
std::string Error(const std::string& operation)
{
    const auto code = GetLastError();
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, message, 512, nullptr);
    return operation + ": Win32=" + std::to_string(code) + " " + Utf8(message);
}
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
    if (!DeviceIoControl(hub, IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION, request, static_cast<DWORD>(buffer.size()),
        request, static_cast<DWORD>(buffer.size()), &returned, nullptr)) throw std::runtime_error(Error("Get descriptor type=" + std::to_string(type)));
    if (returned < sizeof(USB_DESCRIPTOR_REQUEST) || returned > buffer.size()) throw std::runtime_error("Invalid descriptor response size");
    return {buffer.begin() + sizeof(USB_DESCRIPTOR_REQUEST), buffer.begin() + returned};
}
std::string ReadString(HANDLE hub, ULONG port, UCHAR index, USHORT language)
{
    if (index == 0) return "<not supplied>";
    const auto bytes = ReadDescriptor(hub, port, USB_STRING_DESCRIPTOR_TYPE, index, language, 255);
    if (bytes.size() < 2 || bytes[1] != USB_STRING_DESCRIPTOR_TYPE || bytes[0] < 2 || bytes[0] > bytes.size() || bytes[0] % 2 != 0)
        throw std::runtime_error("Malformed USB string descriptor");
    std::wstring value;
    for (std::size_t i = 2; i < bytes[0]; i += 2) value.push_back(static_cast<wchar_t>(bytes[i] | (bytes[i + 1] << 8)));
    return Utf8(value);
}
void ReadDetails(HANDLE hub, ULONG port, const USB_DEVICE_DESCRIPTOR& descriptor, UsbDevice& device)
{
    USHORT language = 0x0409;
    if (descriptor.iManufacturer || descriptor.iProduct || descriptor.iSerialNumber) {
        try {
            const auto languages = ReadDescriptor(hub, port, USB_STRING_DESCRIPTOR_TYPE, 0, 0, 255);
            if (languages.size() < 4 || languages[1] != 3 || languages[0] < 4 || languages[0] > languages.size() || languages[0] % 2 != 0)
                throw std::runtime_error("Malformed USB language descriptor");
            language = static_cast<USHORT>(languages[2] | (languages[3] << 8));
        } catch (const std::exception& error) { device.diagnostics.push_back(std::string(error.what()) + "; trying language 0409"); }
    }
    const auto readString = [&](UCHAR index, std::string& destination, const char* name) {
        try { destination = ReadString(hub, port, index, language); }
        catch (const std::exception& error) { destination = "<unavailable>"; device.diagnostics.push_back(std::string(name) + ": " + error.what()); }
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
        } catch (const std::exception& error) { device.diagnostics.push_back("Configuration " + std::to_string(index) + ": " + error.what()); }
    }
}
}

UsbScanResult WindowsUsbBackend::EnumerateDevices()
{
    UsbScanResult result;
    m_logger.Write("INFO", "USB", "Scanning present USB hubs (read-only descriptor requests)");
    const auto rawSet = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HUB, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (rawSet == INVALID_HANDLE_VALUE) throw std::runtime_error(Error("SetupDiGetClassDevs"));
    DeviceSet devices(rawSet);
    for (DWORD index = 0;; ++index) {
        SP_DEVICE_INTERFACE_DATA interfaceData{};
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices.get(), nullptr, &GUID_DEVINTERFACE_USB_HUB, index, &interfaceData)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) result.errors.push_back(Error("SetupDiEnumDeviceInterfaces"));
            break;
        }
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(devices.get(), &interfaceData, nullptr, 0, &size, nullptr);
        if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) { result.errors.push_back(Error("Get hub path size")); continue; }
        std::vector<std::uint8_t> storage(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devices.get(), &interfaceData, detail, size, nullptr, nullptr)) {
            result.errors.push_back(Error("Get hub path")); continue;
        }
        const std::string hubPath = Utf8(detail->DevicePath);
        Handle hub(CreateFileW(detail->DevicePath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (hub.get() == INVALID_HANDLE_VALUE) { result.errors.push_back(Error("Open hub " + hubPath)); continue; }
        USB_NODE_INFORMATION node{};
        node.NodeType = UsbHub;
        DWORD returned = 0;
        if (!DeviceIoControl(hub.get(), IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof(node), &node, sizeof(node), &returned, nullptr)) {
            result.errors.push_back(Error("Read hub " + hubPath)); continue;
        }
        m_logger.Write("DEBUG", "USB", "Hub=" + hubPath + " ports=" + std::to_string(node.u.HubInformation.HubDescriptor.bNumberOfPorts));
        for (ULONG port = 1; port <= node.u.HubInformation.HubDescriptor.bNumberOfPorts; ++port) {
            // Space for all possible endpoint pipe records in the variable-sized response.
            std::vector<std::uint8_t> connectionBytes(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO));
            auto* connection = reinterpret_cast<USB_NODE_CONNECTION_INFORMATION_EX*>(connectionBytes.data());
            connection->ConnectionIndex = port;
            if (!DeviceIoControl(hub.get(), IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, connection, static_cast<DWORD>(connectionBytes.size()),
                connection, static_cast<DWORD>(connectionBytes.size()), &returned, nullptr)) {
                result.errors.push_back(Error("Read connection " + hubPath + " port=" + std::to_string(port))); continue;
            }
            if (connection->ConnectionStatus == NoDeviceConnected) continue;
            if (connection->ConnectionStatus != DeviceConnected) {
                result.errors.push_back("Hub=" + hubPath + " port=" + std::to_string(port) + " connection status=" + std::to_string(connection->ConnectionStatus)); continue;
            }
            UsbDevice device;
            device.location = hubPath + "/port-" + std::to_string(port);
            device.vendorId = connection->DeviceDescriptor.idVendor;
            device.productId = connection->DeviceDescriptor.idProduct;
            device.activeConfiguration = connection->CurrentConfigurationValue;
            ReadDetails(hub.get(), port, connection->DeviceDescriptor, device);
            LogUsbDevice(m_logger, device);
            result.devices.push_back(std::move(device));
        }
    }
    for (const auto& error : result.errors) m_logger.Write("ERROR", "USB", error);
    m_logger.Write("INFO", "USB", "Scan finished: devices=" + std::to_string(result.devices.size()) + " scan errors=" + std::to_string(result.errors.size()));
    m_logger.Write("INFO", "AA", "Discovery finished; scanning alone does not start an Android Auto session");
    return result;
}
}
