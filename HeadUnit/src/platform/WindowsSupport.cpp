#include "platform/WindowsSupport.h"
#include <windows.h>
#include <setupapi.h>

namespace headunit {
// Closes the handle unless it is the invalid marker.
void WindowsHandleDeleter::operator()(void* handle) const
{
    if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
}

// Destroys the set unless it is the invalid marker.
void DeviceInfoSetDeleter::operator()(void* deviceInfoSet) const
{
    if (deviceInfoSet != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(deviceInfoSet);
}

// UTF-8 text of a UTF-16 Windows string.
std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

// "<operation> failed (Win32 5: Access is denied.)": the last Windows error of this thread, with its system message.
std::string WindowsErrorText(const std::string& operation)
{
    const DWORD code = GetLastError();
    wchar_t message[512]{};
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, message, 512, nullptr);
    std::string text = WideToUtf8(message);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
    return operation + " failed (Win32 " + std::to_string(code) + (text.empty() ? "" : ": " + text) + ")";
}
}
