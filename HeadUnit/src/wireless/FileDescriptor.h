#pragma once

namespace headunit {
// Owns a POSIX file descriptor (a socket, a pipe end) and closes it when it goes out of scope.
class FileDescriptor {
public:
    explicit FileDescriptor(int descriptor = -1);
    ~FileDescriptor();
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    int Get() const;
    bool IsValid() const;
    void Reset(int descriptor = -1);

private:
    int m_descriptor;
};
}
