# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A car head unit (C++20, Qt 6 Widgets) that runs Android Auto over USB (AOA) on Windows and Linux, plus wireless
Android Auto on Linux only. The real target is a Raspberry Pi 4 in a car; development happens on Windows. All code lives
in `HeadUnit/`; `HeadUnit.sln` and `BuildAndRun.sh` sit at the repo root. The user-facing docs (`HeadUnit/README.md`,
script messages) are German; code, comments and `docs/*.md` are English.

Key docs, read before larger changes: `HeadUnit/docs/architecture.md` (the authoritative design description, keep it
current), `docs/wireless.md`, `docs/linux.md`, `docs/progress.md` (dated log of what was changed and what was verified
on hardware vs. not).

## Build and test

There are **two parallel build systems over the same sources**: native MSBuild projects (`HeadUnit.sln`,
`HeadUnit/HeadUnit.vcxproj`, `Aasdk.vcxproj`, `tests/*.vcxproj`, shared settings in `HeadUnit/msbuild/`) and
`HeadUnit/CMakeLists.txt` (+ `cmake/*.cmake`, `CMakePresets.json`). A new source file must be added to `CMakeLists.txt`
**and** `HeadUnit.vcxproj` + `HeadUnit.vcxproj.filters` (and `tests/CoreTests.vcxproj` / `tests/ProtocolTests.vcxproj`
when a test target compiles it: CoreTests builds the portable core, ProtocolTests the core plus `headunit_usb`'s
Windows sources and the video decoder). Linux-only files are `None` items in the vcxproj.

Build profiles, the same in both systems: `debug`, `release`, `debug_level_log`, `release_level_log` (VS configurations
`Debug`, `Release`, `DebugLevelLog`, `ReleaseLevelLog`; CMake configurations of the same names). The level-log profiles
compile exactly like debug/release (release_level_log keeps full optimization) and only define
`HEADUNIT_VERBOSE_LOGGING`, which makes the log default to `trace`. Every build writes to
`HeadUnit/bin/<system><arch>/<profile>/` (e.g. `bin/windowsx64/debug/HeadUnit.exe`, `bin/linuxarm64/release/HeadUnit`),
intermediate files to its `obj/` (CMake presets: `obj/`, core-only: `obj-core/`). Never add other output folders.

Windows (from `HeadUnit/`):
- First time on a machine: `powershell -ExecutionPolicy Bypass -File HeadUnit/scripts/Prepare-Dependencies.ps1` (from repo root).
- Full app: open `HeadUnit.sln` in VS 2026 (toolset v145, `Debug|x64` etc.), or MSBuild `HeadUnit.sln -p:Configuration=Debug -p:Platform=x64`.
  Qt kit is auto-found at `../.tools/Qt/6.8.3/msvc2022_64` (else `QT_ROOT`).
- Portable core + tests only: `cmake --preset windows-core-only`, `cmake --build --preset windows-core-only`, `ctest --preset windows-core-only`.
- CMake full app alternative: set `QT_ROOT`, then presets `windows-debug` / `windows-release` / `windows-debug-level-log` /
  `windows-release-level-log` (configure, build and test presets share the name). CMake does not copy the Qt DLLs next
  to the exe (only `cmake --install` deploys them); put `$QT_ROOT/bin` on `PATH` to run it from `bin/`.

Linux (from `HeadUnit/`): `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
(also `linux-release`, `linux-debug-level-log`, `linux-release-level-log`, `linux-core-only`, and `linux-arm64-*` /
`linux-arm-*` for the Pi). On the Pi the user runs `bash BuildAndRun.sh --pull` from the repo root: it picks the preset
from `uname -m`, builds (log in `bin/<system><arch>/<profile>/build.log`), runs ctest and starts HeadUnit. Usage:
`BuildAndRun.sh [debug|release|debug_level_log|release_level_log] [--clean] [--no-test] [--no-run] [--install-deps]
[--pull] [--reset] [-j N] [--core-only] [--install-udev] [-- <app args>]`; unknown arguments are errors, `--reset` is
destructive. The apt package list is duplicated in `BuildAndRun.sh`, `docs/linux.md` and `.github/workflows/build.yml` —
keep them in sync.

Tests: two plain executables, no test framework. `CoreTests` (portable core, no Qt) and `ProtocolTests` (AASDK TLS,
session and transport stop, input/audio; plus `WirelessTests.cpp` when wireless is enabled). Each test file has one
entry point `RunXxxTests()` declared in `tests/CoreTestSuites.h` / `tests/ProtocolTestSuites.h` and called from the
file's `main()`; its tests live in an anonymous namespace and fail via `Check(cond, message)` (throws). Shared helpers:
`tests/TestSupport.h` (`Check`, `TestLogger`), `tests/FakeTransports.h` (`StoppedTransport`, `SilentTransport`). There is
no per-test filter, so run the whole binary (`ctest -R CoreTests` / `-R ProtocolTests`, or the exe directly). Debug
builds on Windows show a modal dialog on a failed debug assertion (e.g. an out-of-range iterator), which looks like a
hang: run the exe with a timeout.

CI (`.github/workflows/build.yml`): Linux full build (`linux-release`) + tests + `HeadUnit --help`/`--scan`, and Windows
core-only. The full Windows Qt build is not in CI. `src/wireless/*` and `#ifdef HEADUNIT_WIRELESS` code only compiles on
Linux (needs Qt6 DBus), so on Windows it can only be reviewed by reading; CI or the Pi catches errors there.

Remote API for other programs (`HeadUnit/docs/api.md`): JSON lines over TCP on 127.0.0.1:47050 (`--api-port`,
`HEADUNIT_API_PORT`); `RemoteCommand` in the core, `RemoteServer` + `MainWindow::StartRemoteApi` in the app; `GpioBridge.py`
in the repo root is the client for the real buttons and encoder. A new console action must be added to the API as well.

App diagnostics (no window needed for most): `--scan`, `--probe-usb`, `--start-accessory`, `--repair-driver`,
`--recover-phone`, `--test-projection`, `--test-input`, `--test-audio`, `--test-tone`, `--test-console`, `--test-keys`,
`--smoke-test`, `--display WxH` (parsed in `src/CommandLine.cpp`). Useful env vars: `HEADUNIT_LOG_LEVEL=trace|debug|info|warning|error`,
`HEADUNIT_PROTOCOL_TRACE=1`, `HEADUNIT_TEST_SHOTS=<dir>`, `HEADUNIT_TEST_KEYS`, `HEADUNIT_TEST_PAIRING=1` (with
`--smoke-test`: shows the Bluetooth pairing page), `HEADUNIT_TEST_VOLUME=1` (with `--smoke-test`: shows the volume
bar), `HEADUNIT_TEST_PAGE=<tile id>` (with `--smoke-test`: opens that tile's page, e.g. `Bluetooth` with made-up phones,
`Settings`), `HEADUNIT_MUSIC_DIR`, `HEADUNIT_USB_BACKEND=libusb`, `HEADUNIT_WIFI_*`, `HEADUNIT_BT_NAME`.
`headunit.log` is appended in the working directory; lines carry a level and a `[TAG]` such as
`WATCH`, `BT`, `WLAN`, `AA`, `USB`, `REPAIR`, `AUDIO` (`logger.Write(LogLevel::Info, "WATCH", ...)`). Without a phone
attached, the default (automatic) mode will connect any plugged-in Android phone on its own — check `--scan` first when
smoke-running.

## Architecture (big picture)

CMake targets, layered:
- `headunit_core` — portable, **no Qt and no OS headers**: USB descriptor parsing, `AndroidDeviceDetector`,
  `AoaNegotiator`, `AutoConnect` (the connect state machine), `PhoneWatch` (the automatic mode loop), `Logger`/`LogLevel`,
  `CommandLine`. Logic here takes its dependencies as injected structs/callbacks (`AutoConnectDeps` etc.) so CoreTests
  can script them. Also the portable pieces used by the UI and tested in CoreTests (`DisplayConfig`, `TouchMapping`,
  `PhoneScreenDetector`, `ConsoleController`, `ProjectionInput`, `TransportReceiveBuffer`, `ui/HomeMenuLayout`,
  `ui/KnobZones`, `ui/HomeTileSetup`, `ui/PageFocus.h`, `audio/MediaActivity`, `audio/PcmRingBuffer`,
  `media/MusicLibrary`, `media/StreamText`).
- `headunit_usb` — OS adapter + session: libusb (`LibusbHandles`, `LibusbControl`), `ProjectionTransport`,
  `AndroidUsbProbe`, `AndroidAutoSession` → `ProjectionSession` with its channels (`AudioSinkChannel`,
  `MicrophoneChannel`, `DisplayService`, `ServiceDiscovery`; AASDK framing/TLS; no Qt, no Win32), USB discovery backend,
  driver repair, `AudioEngine`. `headunit_video` (FFmpeg decoder) and `headunit_media` (music file player) sit beside it.
- `headunit_wireless_link` + `headunit_wireless` — Linux only. The link library has no Qt: the Bluetooth message framing
  and conversation (`WirelessFrameParser`, `WirelessHandshake`, `WirelessLink`), `TcpListener` and `SocketTransport` (TCP
  twin of `ProjectionTransport`); ProtocolTests links it. `headunit_wireless` adds the hotspot (`Hotspot`, `nmcli`), the
  BlueZ service (`BluetoothService` with `BluetoothContext`, `BluezCalls`, and the D-Bus objects `PairingAgent`,
  `AndroidAutoProfile`, `DeviceWatcher`) and `WirelessStation`. It is the **only** target that uses moc; the rest of the
  code base has no `Q_OBJECT`.
- `HeadUnit` executable — `main.cpp` (composition, CLI modes), `platform/ShutdownSignal`, `src/ui/` (MainWindow,
  CarPanel/RotaryKnob/AudioDisplay, VideoWidget, radio pages, `ScriptedPhoneTest` for the `--test-*` window modes),
  `src/radio/RadioBrowser`.
- `third_party/aasdk` — vendored OpenCarDev AASDK (GPL-3.0) with local patches recorded in `third_party/aasdk/PATCHES.md`;
  record any further change there. `third_party/miniaudio`, `third_party/libusb` (Windows only) are unmodified.

Platform differences are confined to a few seams chosen in factories, not `#ifdef`s scattered through code: USB
discovery (`IUsbBackend`: `WindowsUsbBackend` / `LibusbUsbBackend`, chosen in `CreateUsbBackend`), pre-open device
handling (`usb/DriverRepair.h`, implemented by `WindowsDriverRepair.cpp` WinUSB/UAC or `LinuxDriverRepair.cpp`
udev/sysfs), audio output (`OpenPlatformPcmStream`: `WasapiPcmStream` or `MiniaudioPcmStream`, both on
`QueuedPcmStream`), Win32 helpers in `platform/WindowsSupport`, and signal handling in `platform/ShutdownSignal`.
Linux backends also compile on Windows for testing.

Runtime flow: `MainWindow` starts one worker running `RunPhoneWatch` for the window's lifetime. Each round it does a
quiet libusb scan keyed by **serial number** (a phone keeps its identity across the AOA re-enumeration), connects a newly
plugged phone via `ConnectPhoneAutomatically`, and between rounds waits for a wireless phone (`WirelessStation`: hotspot
first, then Bluetooth). Either transport feeds the same `RunAndroidAutoSession`. Two stop flags: `m_isStopRequested`
(end current attempt) and `m_isWatchStopRequested` (end watch). The session is driven by a 100 ms tick with watchdogs;
stopping must join transport workers and drain AASDK completions before destroying the Asio context (see
architecture.md "Session lifecycle" before touching shutdown code).

Video: FFmpeg H.264 → RGB into a latest-frame mailbox, consumed by a 33 ms Qt timer. `DisplayConfig`/`VideoLayoutOf`
map the display (any pixel size; the car window takes the screen's exact size, larger than 1920x1080 is scaled down) to a fixed AA resolution plus margins; video crop and
touch mapping share the shown-area coordinate space. Phone screen state is inferred from pixels (`DetectPhoneScreen`),
since the protocol does not report it.

UI: `QStackedWidget` with `VideoWidget` and radio pages (`MenuPage` subclasses: `HomeMenu`, `SettingsPage`,
`PlayerPage` → `MultimediaPage`/`RadioPage`, and `PairingPage`, which goes in front of everything while a phone pairs),
drawn from `docs/design/home-menu.svg` via `ui/MenuStyle` (no QtSvg). Hard keys go through `ConsoleController` →
`ConsoleEffect`; knob input goes to the front page or the phone (`SendKey`, `m_localKeys` keeps press/release on the
same side). Persisted settings use `QSettings()` with the organization/application name set once in `main`.

## Conventions

The global C++ coding standard (user CLAUDE.md) applies in full; project specifics and deliberate exceptions:
- Naming: PascalCase types, functions and file names, `camelCase` variables, `m_camelCase` members of classes,
  `isX`/`hasX`/`canX`/`shouldX`/`wasX` booleans, `kName` constants, namespace `headunit`. Plain data structs (only public
  fields, no invariants, e.g. `UsbDevice`, `AutoConnectResult`, `HotspotConfig`) keep plain field names without `m_`.
- Formatting stays as it is: 4-space indentation; functions in `src/` put the opening brace on its own line, tests keep
  it on the signature line.
- Every function has a one-line (or short) description above its definition; member methods are described in the `.cpp`
  only, never again in the `.h`. Every class has its description above the declaration in the `.h`.
- One class per file pair; small helper types that only serve one class may stay with it. Free helper functions of a
  `.cpp` go into an anonymous namespace.
- Ownership through `std::unique_ptr` / `std::shared_ptr`; no manual `delete`. Exception: Qt widgets and D-Bus adaptors
  are created with `new` and a parent that owns them; member pointers to such objects are non-owning.
- Callbacks from UI classes are set through `SetXHandler(...)` methods and kept in private `m_onX` members, not public
  `std::function` fields.
- Warnings are errors in spirit: `/W4 /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic`; builds should stay warning-free
  in own sources.
- Keep new logic in the portable core where possible and cover it in CoreTests with scripted dependencies; keep Qt/OS
  code thin.
- After a feature, add a dated entry to `docs/progress.md` separating what was verified (and where) from what was not,
  and update `docs/architecture.md` when structure changes.
- The user tests on the Pi with a Samsung SM-F776B and commits/pushes themselves; don't commit unless asked.
