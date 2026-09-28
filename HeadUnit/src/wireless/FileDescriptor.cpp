#include "wireless/FileDescriptor.h"
#include <unistd.h>

namespace headunit {
// Takes over `descriptor` (-1: none).
FileDescriptor::FileDescriptor(int descriptor) : m_descriptor(descriptor)
{
}

// Closes the descriptor.
FileDescriptor::~FileDescriptor()
{
    Reset();
}

// The descriptor, -1 for none.
int FileDescriptor::Get() const
{
    return m_descriptor;
}

// Whether there is a descriptor.
bool FileDescriptor::IsValid() const
{
    return m_descriptor >= 0;
}

// Closes the descriptor held so far and takes over `descriptor`.
void FileDescriptor::Reset(int descriptor)
{
    if (m_descriptor >= 0) ::close(m_descriptor);
    m_descriptor = descriptor;
}
}
