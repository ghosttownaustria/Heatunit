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

## Current build

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

Update: libusb 1.0.30 is now linked for the real USB access/AOA probe. Its Windows
x64 DLL/import library and header are in `third_party/libusb` with license and
source/hash records. Native MSBuild and CMake both deploy the DLL automatically.
No AASDK, protobuf, OpenSSL, FFmpeg or spdlog is currently linked.
The logger is a small standard C++ implementation. Dependencies are discovered
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

## Later candidates (not installed or linked)

- libusb (now integrated for probing): LGPL-2.1-or-later; cross-platform USB access. [License](https://github.com/libusb/libusb/blob/master/COPYING),
  [Windows driver constraints](https://github.com/libusb/libusb/wiki/Windows).
- AASDK/OpenAuto: GPLv3 declarations; exact files/fork revisions need review before
  adoption. Research pins and platform caveats are in `android_auto.md`.
- OpenSSL and protobuf: use only when introducing the protocol adapter; lock
  compatible releases then rather than installing unused current versions now.
- FFmpeg: primarily LGPL-2.1-or-later, optional build components change the license.
  Select an appropriate decoder build explicitly. [Upstream license notes](https://ffmpeg.org/legal.html).

## Core-only tests

On Windows use the `core-only` configure and `core-debug` build/test presets.
For a Linux development environment with a C++20 compiler:

```sh
cmake -S HeadUnit -B HeadUnit/out/core -DHEADUNIT_BUILD_APP=OFF -DBUILD_TESTING=ON
cmake --build HeadUnit/out/core
ctest --test-dir HeadUnit/out/core --output-on-failure
```

Linux discovery/GUI backend selection will be added in its own milestone.
