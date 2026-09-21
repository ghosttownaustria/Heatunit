# Dependencies and reproducibility

## Native Visual Studio 2026 build (preferred on this workstation)

Open repository-root `HeadUnit.sln`. Native MSBuild projects use v145 and the
installed Qt 6.8.3 SDK, with no CMake dependency for this workflow. Verified with
VS2026 Insiders / MSVC 14.51.36231, Debug and Release x64. Microsoft's
[VS2026 upgrade guidance](https://devblogs.microsoft.com/cppblog/upgrading-c-projects-to-visual-studio-2026/)
describes v145 and compatibility with earlier MSVC-built libraries. Both local
Qt runtime smoke tests passed; this is not a claim of an upstream Qt-certified
VS2026 configuration. `msbuild/Qt.targets` deploys the minimal current runtime.
See README.md for F5/build instructions and SDK-path overrides.

## Linux build

System packages, no vcpkg and no downloads (CMake never downloads code). The package list, the udev rule and
the first-run checks are in [linux.md](linux.md). What the CMake build needs on Linux:

| Dependency | How it is found | Notes |
| --- | --- | --- |
| C++20 compiler, CMake 3.24+, Ninja | system | GCC 11+ (needs `<chrono>` calendar types, `std::span`) |
| Qt 6.4+ Widgets, Concurrent | `find_package(Qt6 6.4 ...)` | Windows uses 6.8.3; the UI sources were compile-checked against the 6.4.3 headers (Ubuntu 22.04/24.04 and Debian 12 ship 6.2/6.4), not linked or run with them |
| Boost 1.74+ (Asio, Endian: headers only) | `find_package(Boost)`, target `Boost::headers` | |
| OpenSSL 3, protobuf + protoc | `find_package(OpenSSL)`, `find_package(Protobuf)` | protoc generates the AASDK sources at build time |
| libusb 1.0.16+ | pkg-config `libusb-1.0` | Windows uses the vendored 1.0.30 DLL |
| FFmpeg libavcodec, libavutil, libswscale (H.264 decoder) | pkg-config | |
| miniaudio 0.11.25 | vendored header `third_party/miniaudio` | Loads libpulse/libasound at run time; no development package needed |

The same CMake files build on Windows against the vcpkg tree (`HEADUNIT_DEPS`, default `../.tools/vcpkg/installed/x64-windows`
next to the repository), which `scripts/Prepare-Dependencies.ps1` prepares. `scripts/Generate-Protobuf.ps1` and the
`msbuild/` files belong to the Visual Studio projects only.

## Current build (Windows)

| Dependency | Selected/tested version | Purpose |
| --- | --- | --- |
| CMake | minimum 3.24; VS bundled CMake used | Build, presets, CTest, deployment |
| MSVC / Windows SDK | VS2022 v143 x64 / SDK 10.0.26100.0 | C++20, SetupAPI and USB IOCTLs |
| Qt | 6.8.3, win64_msvc2022_64, qtbase only | Core/Gui/Widgets/Concurrent |

Qt 6.8 was selected for the requested Windows 10/11 and MSVC 2022 target, rather
than silently selecting the newest Qt with potentially different platform
requirements. See [Qt 6.8 Windows support](https://doc.qt.io/qt-6.8/windows.html).
The Widgets/Concurrent build uses dynamic Qt libraries; Qt provides LGPLv3/GPL
and commercial licensing options. See the [Qt source license texts](https://github.com/qt/qtbase/tree/v6.8.3/LICENSES).
The local SDK includes its license files. No project-wide license is assigned here.

Update: libusb 1.0.30 is linked for the real USB access/AOA session. Its Windows
x64 DLL/import library and header are in `third_party/libusb` with license and
source/hash records. Native MSBuild and CMake both deploy the DLL automatically.
AASDK, protobuf, OpenSSL, Boost.Asio and FFmpeg are linked as well (see the Windows
build steps in README.md); spdlog is not used. The logger is a small standard C++
implementation. Dependencies are discovered
via `find_package` and `QT_ROOT`; CMake does not silently download code.

The following optional bootstrap reproduces the local Qt setup from the repository
root (Python 3.13 was used). Qt binaries are downloaded from Qt mirrors by aqt.
An existing equivalent official Qt installation works as well.

```powershell
py -3 -m venv .tools/aqt
.\.tools\aqt\Scripts\python.exe -m pip install aqtinstall==3.3.0
.\.tools\aqt\Scripts\python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O .tools/Qt --archives qtbase
```

SDK and build outputs are ignored by Git. Qt version/archive selection is pinned;
Python transitive bootstrap dependencies are not fully locked. For distribution,
retain exact dependency source/license notices matching the shipped Qt binaries;
the CMake install target is a runtime deployment convenience, not a complete
redistribution package.

## Licenses of what is linked

- libusb: LGPL-2.1-or-later; cross-platform USB access. [License](https://github.com/libusb/libusb/blob/master/COPYING),
  [Windows driver constraints](https://github.com/libusb/libusb/wiki/Windows).
- AASDK/OpenAuto: GPL-3.0-or-later (see `third_party/aasdk/PATCHES.md`); the application inherits that license.
  Research pins and platform caveats are in `android_auto.md`.
- FFmpeg: primarily LGPL-2.1-or-later, optional build components change the license.
  Select an appropriate decoder build explicitly. [Upstream license notes](https://ffmpeg.org/legal.html).
- miniaudio: public domain or MIT-0. [License](https://github.com/mackron/miniaudio/blob/master/LICENSE).

## Core-only tests

On Windows use the `core-only` configure and `core-debug` build/test presets; on Linux `linux-core-only`
(a C++20 compiler and CMake are enough):

```sh
cd HeadUnit
cmake --preset linux-core-only
cmake --build --preset linux-core-only
ctest --preset linux-core-only
```

The same works without presets: `cmake -S HeadUnit -B HeadUnit/out/core -DHEADUNIT_BUILD_APP=OFF -DBUILD_TESTING=ON`.
