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
`HeadUnit/CMakeLists.txt` (+ `CMakePresets.json`). A new source file must be added to `CMakeLists.txt` **and**
`HeadUnit.vcxproj` + `HeadUnit.vcxproj.filters` (and `tests/CoreTests.vcxproj` if it belongs to the portable core).
Linux-only files are `None` items in the vcxproj.

Windows (from `HeadUnit/`):
- First time on a machine: `powershell -ExecutionPolicy Bypass -File HeadUnit/scripts/Prepare-Dependencies.ps1` (from repo root).
- Full app: open `HeadUnit.sln` in VS 2026 (toolset v145, Debug|x64), or MSBuild `HeadUnit.vcxproj /p:Configuration=Debug /p:Platform=x64` → `out/vs2026/x64/Debug/HeadUnit.exe`. Qt kit is auto-found at `../.tools/Qt/6.8.3/msvc2022_64` (else `QT_ROOT`).
- Portable core + tests only: `cmake --preset core-only`, `cmake --build --preset core-debug`, `ctest --preset core-debug`.
- CMake full app alternative: set `QT_ROOT`, then presets `windows-vs2022` / `windows-debug`.

Linux (from `HeadUnit/`): `cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug`
(also `linux-release`, `linux-core-only`). On the Pi the user runs `bash BuildAndRun.sh --pull` from the repo root
(builds, runs ctest, starts HeadUnit; `--help` for options such as `--core-only`, `--no-run`, `-j 2`, `-- <app args>`).
The apt package list is duplicated in `BuildAndRun.sh`, `docs/linux.md` and `.github/workflows/build.yml` — keep them in sync.

Tests: two plain executables, no test framework. `CoreTests` (portable core, no Qt) and `ProtocolTests` (AASDK
framing/TLS, transport stop, input/audio; plus `WirelessTests.cpp` when wireless is enabled). Each `main()` calls
`TestXxx()` functions in sequence and fails via `Check(cond, message)` throwing; there is no per-test filter, so run the
whole binary (`ctest -R CoreTests` / `-R ProtocolTests`, or run the exe directly). New test files must be added to the
test target in `CMakeLists.txt` and the matching test `.vcxproj`.

CI (`.github/workflows/build.yml`): Linux full build + tests + `HeadUnit --help`/`--scan`, and Windows core-only. The
full Windows Qt build is not in CI. `src/wireless/*` and `#ifdef HEADUNIT_WIRELESS` code only compiles on Linux (needs
Qt6 DBus), so on Windows it can only be reviewed by reading; CI or the Pi catches errors there.

App diagnostics (no window needed for most): `--scan`, `--probe-usb`, `--start-accessory`, `--repair-driver`,
`--recover-phone`, `--test-projection`, `--test-input`, `--test-audio`, `--test-tone`, `--test-console`, `--test-keys`,
`--smoke-test`, `--display WxH`. Useful env vars: `HEADUNIT_PROTOCOL_TRACE=1`, `HEADUNIT_TEST_SHOTS=<dir>`,
`HEADUNIT_TEST_KEYS`, `HEADUNIT_TEST_PAIRING=1` (with `--smoke-test`: shows the Bluetooth pairing page), `HEADUNIT_MUSIC_DIR`, `HEADUNIT_USB_BACKEND=libusb`, `HEADUNIT_WIFI_*`, `HEADUNIT_BT_NAME`.
`headunit.log` is appended in the working directory; lines carry a level and a `[TAG]` such as `WATCH`, `BT`,
`WLAN`, `AA`, `USB`, `REPAIR`, `AUDIO` (`logger.Write("INFO", "WATCH", ...)`). Without a phone attached, the default (automatic) mode will connect
any plugged-in Android phone on its own — check `--scan` first when smoke-running.

## Architecture (big picture)

CMake targets, layered:
- `headunit_core` — portable, **no Qt and no OS headers**: USB descriptor parsing, `AndroidDeviceDetector`,
  `AoaNegotiator`, `AutoConnect` (the connect state machine), `PhoneWatch` (the automatic mode loop), `Logger`. Logic
  here takes its dependencies as injected structs/callbacks (`AutoConnectDeps` etc.) so CoreTests can script them.
  Also header-only portable pieces used by the UI and tested in CoreTests (`DisplayConfig.h`, `ConsoleController.h`,
  `ui/HomeMenuLayout.h`, `ui/KnobZones.h`, `audio/AudioFocus.h`, `media/MusicLibrary.h`, `ProjectionInput.h`).
- `headunit_usb` — OS adapter + session: libusb session/`ProjectionTransport`, `AndroidAutoSession` (AASDK
  framing/TLS/channels; no Qt, no Win32), USB discovery backend, driver repair, audio engine.
- `headunit_wireless` — Linux only: hotspot (`nmcli`), BlueZ RFCOMM service (QtDBus), `SocketTransport` (TCP twin of
  `ProjectionTransport`). The **only** target that uses moc; the rest of the code base has no `Q_OBJECT`.
- `HeadUnit` executable — `main.cpp` (composition, CLI modes) and `src/ui/` (MainWindow, CarPanel/knob, VideoWidget,
  radio pages), `src/radio/RadioBrowser`.
- `third_party/aasdk` — vendored OpenCarDev AASDK (GPL-3.0) with local patches recorded in `third_party/aasdk/PATCHES.md`;
  record any further change there. `third_party/miniaudio`, `third_party/libusb` (Windows only) are unmodified.

Platform differences are confined to three interfaces chosen in factories, not `#ifdef`s scattered through code:
USB discovery (`IUsbBackend`: `WindowsUsbBackend` / `LibusbUsbBackend`), pre-open device handling (`DriverRepair.cpp`
WinUSB/UAC vs. `DriverRepairLinux.cpp` udev/sysfs), and audio (`IAudioEngine`: WASAPI / miniaudio). Linux backends also
compile on Windows for testing.

Runtime flow: `MainWindow` starts one worker running `RunPhoneWatch` for the window's lifetime. Each round it does a
quiet libusb scan keyed by **serial number** (a phone keeps its identity across the AOA re-enumeration), connects a newly
plugged phone via `ConnectPhoneAutomatically`, and between rounds waits for a wireless phone (`WirelessStation`: hotspot
first, then Bluetooth). Either transport feeds the same `RunAndroidAutoSession`. Two stop flags: `m_isStopRequested`
(end current attempt) and `m_isWatchStopRequested` (end watch). The session is driven by a 100 ms tick with watchdogs;
stopping must join transport workers and drain AASDK completions before destroying the Asio context (see
architecture.md "Session lifecycle" before touching shutdown code).

Video: FFmpeg H.264 → RGB into a latest-frame mailbox, consumed by a 33 ms Qt timer. `DisplayConfig`/`VideoLayoutOf`
map a chosen display (800x480, 1280x720, 1600x600, 1920x1080) to a fixed AA resolution plus margins; video crop and
touch mapping share the shown-area coordinate space. Phone screen state is inferred from pixels (`DetectPhoneScreen`),
since the protocol does not report it.

UI: `QStackedWidget` with `VideoWidget` and radio pages (`MenuPage` subclasses: `HomeMenu`, `SettingsPage`,
`MultimediaPage`, `RadioPage`, and `PairingPage`, which goes in front of everything while a phone pairs), drawn from `docs/design/home-menu.svg` via `ui/MenuStyle` (no QtSvg). Hard keys go
through `ConsoleController` → `ConsoleEffect`; knob input goes to the front page or the phone (`SendKey`,
`m_localKeys` keeps press/release on the same side). Persisted settings use `QSettings`.

## Conventions

- Naming: PascalCase types and functions, `m_camelCase` members, `isX`/`hasX` booleans, namespace `headunit`.
- Warnings are errors in spirit: `/W4 /permissive-` (MSVC) and `-Wall -Wextra -Wpedantic`; builds should stay warning-free
  in own sources.
- Keep new logic in the portable core (or header-only portable helpers) where possible and cover it in CoreTests with
  scripted dependencies; keep Qt/OS code thin.
- After a feature, add a dated entry to `docs/progress.md` separating what was verified (and where) from what was not,
  and update `docs/architecture.md` when structure changes.
- The user tests on the Pi with a Samsung SM-F776B and commits/pushes themselves; don't commit unless asked.
