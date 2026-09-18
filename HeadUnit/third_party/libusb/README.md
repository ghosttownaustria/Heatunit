# libusb 1.0.30 (Windows x64)

Unmodified upstream header and VS2022/MS64 DLL/import library. The same DLL is
used for Debug/Release (C ABI; libusb owns/frees its allocations).

Source and license: [v1.0.30 source](https://github.com/libusb/libusb/tree/v1.0.30),
LGPL-2.1-or-later, see `COPYING` and the original notices in `include/libusb.h`.
Corresponding [source archive](https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2).
No modifications to libusb. Preserve source/license availability when redistributing.

Downloaded from the [official release archive](https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.7z).
Extract `include/libusb.h` and `VS2022/MS64/dll/libusb-1.0.{dll,lib}` with 7-Zip;
copy them to this directory's include/ and x64/ folders. No driver installation
is performed by copying this userspace library. Other archive architectures are
not used. SHA-256 values recorded from the downloaded artifacts:

```text
archive  7FB1DFEC805B97983763D7D0AE244320DA12ADD1003D4249C96CC4D586398C79
header   A61260AB145B051B86DF2B0575956F01810190ABE2C7DF6ACA831C33BDC8082C
DLL      7CBF37E76DAE9C840C7E8DBF7348EE8897DCC86C8BA45E46ADA60B89411569F7
LIB      82228E970814628384615793F1C8F2BCA6490A0E22C73FD57292D2F8D028F66D
```
