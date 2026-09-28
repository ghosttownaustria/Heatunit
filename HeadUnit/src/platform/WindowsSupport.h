#pragma once
#include <memory>
#include <string>

namespace headunit {
// Closes a Windows handle (a file, a process, a token); INVALID_HANDLE_VALUE is left alone.
struct WindowsHandleDeleter {
    void operator()(void* handle) const;
};

// Destroys a SetupAPI device information set.
struct DeviceInfoSetDeleter {
    void operator()(void* deviceInfoSet) const;
};

using WindowsHandle = std::unique_ptr<void, WindowsHandleDeleter>;
using DeviceInfoSet = std::unique_ptr<void, DeviceInfoSetDeleter>;

std::string WideToUtf8(const std::wstring& text);
std::string WindowsErrorText(const std::string& operation);
}
