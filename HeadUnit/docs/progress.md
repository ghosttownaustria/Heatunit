# Progress

## 2026-09-19 (later still): touch, rotary knob and keys, audio playback

- **Input:** mouse on the picture becomes touch (`InputReport.touch_event`, coordinates mapped from the
  letterboxed picture into the 800x480 touch space, moves paced at ~125 Hz). A simulated centre console
  sends the rotary controller (`RelativeEvent`, `KEYCODE_ROTARY_CONTROLLER`, one detent per 15 degrees),
  the four nudge keys, Enter, Home, Back, Media, Navigation, Phone and track/play keys. Keyboard shortcuts
  mirror them. The service description now lists these key codes; the phone bound all 20 of them.
  Events cross from the GUI thread to the protocol thread through `ProjectionInput` (token based
  attach/detach, so a finished session can never detach its successor).
- **Audio:** the three audio sinks (media 48 kHz stereo, guidance and system 16 kHz mono) play through
  WASAPI shared mode with a per-stream ring buffer (500 ms, oldest audio dropped when late, 60 ms
  prime), AUTOCONVERTPCM for any output format, master volume in 30 steps (squared gain), mute, and
  level meters for the on-screen audio display. Qt Multimedia is not part of the local Qt install, so
  no extra dependency was added.
- **Verified on the Samsung SM-F776B** (Debug and Release): `--test-input` showed the blue rotary
  focus ring after rotary/direction keys and the map opening after a touch tap in the middle
  (screenshots via `HEADUNIT_TEST_SHOTS`); `--test-audio` played Spotify at volume 5/30 for three
  seconds: 581,632 bytes rendered, 0 bytes dropped, 0 underruns, and Windows' own audio-session meter
  for `HeadUnit.exe` read a peak of 0.0148 (matches the gain). The full stale-Android-Auto recovery
  (`TLS record decode` -> USB restart, attempt 2 with a longer off time -> new session) ran end to end
  once during these tests.
- **Not done:** microphone capture (voice commands, calls), audio focus ducking, night-mode switch.
- **Tests:** CoreTests cover the touch mapping, input bus, ring buffer, PCM peak/gain and audio state;
  ProtocolTests cover the protocol messages for touch, keys and rotary and the session's attach/detach
  of the input bus.

## 2026-09-19 (later): why the one-button flow never got past "connected to the car"

- **Root cause:** after AOA START the app looked for the accessory device "on the same USB port" as
  the phone. The phone's port is not stable: in file-transfer mode it links at SuperSpeed
  (`root_hub30 ... port-2`, bulk `maxPacket=1024`), in accessory mode it often links at USB 2.0 and
  shows up on another root-hub port (`port-6`) or even another root hub. The phone had switched
  ("Mit dem Fahrzeug verbunden"), but the app never saw it, waited 30 s, and its recovery steps (USB
  restart, repeated AOA START) made it worse. The evidence is in the old Release log (accessory on
  `2bce96aa/port-6` and `2c35141/port-8` while the phone sat on `2bce96aa/port-2`).
- **Fix:** the accessory device is found by the phone's **serial number** (read through libusb from
  every accessory-mode candidate; the port is only logged). Verified on hardware right after the
  fix: normal mode on port 2 -> accessory on bus 1 port 6 found after 912 ms -> video.
- **Retry ladder is now split by cause:** the phone did not switch to accessory mode (locked phone /
  prompt) -> plain retries (max. 2), no USB restart and no admin prompt; accessory mode up but
  Android Auto silent (no version answer, or `TLS record decode` from a stale Android Auto) -> USB
  restart of the phone with driver check (max. 2). Version requests are repeated every 2 s (max. 10).
- **USB restart helper:** disabling the accessory device node for 1.5 s did not always make the
  phone leave accessory mode; 3 s did (verified repeatedly). The helper now waits 3 s, checks what
  the phone comes back as and retries with longer off times (3 / 5.5 / 8 s).
- **Hardware results:** 6 consecutive `--test-projection` runs (in-place restart) and 3 runs after a
  helper restart (normal mode -> accessory, incl. the port-6 case) all reached real video (exit 0),
  Debug and Release; also after killing the app mid-session. Not reproduced on demand: the "stale
  Android Auto" (`TLS record decode` right after an in-place restart) seen once; the ladder handles
  it by design but it was not exercised end to end.

## 2026-09-19: one button, automatic recovery

- The window has one button. `RunAutoConnect` (`src/androidauto/AutoConnect.cpp`, portable and
  unit-tested with 11 scripted scenarios in `tests/AutoConnectTests.cpp`) finds the phone, repairs
  the USB driver when Windows reverted it, starts Android Auto and, when the phone never answers
  the version request (20 s, previously up to 90 s), restarts the phone's USB connection and
  retries (max. 2 recoveries, max. 3 driver repairs). Progress is shown step by step.
- Failure seen on hardware: restarting Android Auto in place (AOA START on a phone already in
  accessory mode) worked repeatedly, then failed five times in a row (`USB write deadline after
  0 of 10 bytes`, phone not reading the accessory endpoint). Likely a phone-side state (phone
  locked or Android Auto not launching); not confirmed.
- Recovery, verified on hardware in a scratch program: disabling and re-enabling the accessory
  device node (elevated) makes the phone re-enumerate and leave accessory mode, i.e. it comes back
  as `04E8:6860` like after a cable replug. `libusb_reset_device` and `IOCTL_USB_HUB_CYCLE_PORT`
  do not do this on Windows (no re-enumeration / Win32 31 on the xHCI root hub).
  `HeadUnit.exe --recover-phone` does restart + driver repair in one elevated process
  (one UAC prompt).
- After such a restart the phone shows a single MTP interface (not MTP + modem). The composite
  driver is not offered as "compatible" then, so `--repair-driver` selects it from the USB class
  list (`DI_FLAGSEX_ALLOWEXCLUDEDDRVS`, hardware ID `USB\COMPOSITE`), after which `MI_00` receives the
  existing WinUSB package. Verified on hardware: WinUSB bound and the following AOA START
  accepted. **Not verified:** the complete flow up to video after the recovery; the phone dropped
  off USB entirely during the last test run and needed a physical replug.
- `Repair-PhoneDriver.ps1` and the device list / probe buttons are gone from the window.

## 2026-09-18 (later): reconnect without replug, in-window driver repair

- Cause of the recurring `LIBUSB_ERROR_NOT_FOUND`: Windows re-selects Samsung's driver
  whenever the phone re-appears as `04E8:6860` (setupapi log, Kernel-PnP event 442), so the
  forced `usbccgp`/WinUSB binding is lost each time. Details: `windows_connection.md`.
- Reconnect: a phone already in accessory mode gets AOA identity + START again, which makes
  Android relaunch Android Auto. **Verified on hardware** (Samsung SM-F776B): three
  consecutive `--test-projection` runs, each with real video and a clean ByeBye stop,
  without replugging. Version requests are repeated (max. 6, every 4 s) until the phone
  answers; waiting for the accessory device is now 30 s (was 15 s, too short once) and
  honours the Stop button.
- Repair: `HeadUnit.exe --repair-driver` (elevated, started by the window via UAC when a
  driver error occurs, then rescan and automatic connect). The elevated helper and the
  window flow are new; only the "nothing to repair" paths were exercised on hardware,
  the actual rebinding needs the phone in file-transfer mode after a replug.
- `scripts/Repair-PhoneDriver.ps1` was removed (replaced by the in-app repair).

## 2026-09-18: connection lifecycle (start, stability, clean shutdown)

- **Clean shutdown:** a user stop now sends `ByeByeRequest(USER_SELECTION)` and waits
  up to 2 s for the phone's answer before the transport is stopped. Before TLS is
  established the session ends immediately. Aim: the phone leaves Android Auto
  properly so the next connect does not require replugging the cable. **Not yet
  verified on hardware.**
- **Transport:** `stop()` is idempotent (previously joined the same thread pools
  twice), requests after `stop()` are rejected instead of silently dropped, USB stalls
  are cleared with CLEAR_HALT (max. 3 in a row), errors carry a readable libusb text.
- **Session:** liveness watchdog (30 s), stage-specific startup timeout message,
  undecodable video packets are dropped instead of ending the session, a throwing
  handler no longer skips queued completions, leak check after shutdown.
- **UI:** one state machine controls all buttons; the only Android phone is used
  automatically when nothing is selected; closing the window mid-session ends it
  cleanly first; automatic rescan 1.5 s after a session; the last video frame is
  cleared when the session ends.
- **Tests:** ProtocolTests now cover stopping a session that is waiting for the phone
  (prompt end, transport stopped, no leaked session) and repeated transport `stop()`
  with rejected follow-up requests. Debug build, CoreTests, ProtocolTests and the GUI
  smoke run pass. The graceful ByeBye path itself needs a real phone or a scripted
  TLS peer and is untested.


## 2026-09-18: native protocol/video implementation and real handshake

Added the pinned AASDK native static-library project, automatic protoc generation,
vcpkg dependency preparation, bounded libusb bulk transport, cancellation,
control/video/input/sensor service handling, H.264 FFmpeg decoding and a bounded
latest-frame mailbox feeding Qt. The Connect action combines AOA switching and
the protocol session. Audio playback and touch event transmission remain absent.

Debug build and core/protocol tests pass. TLS regression tests exercise TLS 1.2
and 1.3 in both directions, including multi-record payloads. CMake app configure
passes with the prepared dependencies. Dependency builds completed locally.

The real Samsung accepted Android Auto **1.7**, completed TLS and sent service
discovery. The headunit sent its response, but the phone did not open a video
channel; the first run ended at its 90-second deadline. A keepalive attempt after
discovery encountered a USB write timeout. **No projected video is confirmed.**
Artifacts: `out/projection-live-test.log`, `out/projection-fresh-trace.log`.

Corrected upstream version byte order (previously printed 256.1792), suppressed
false SSL_ERROR_WANT_READ error logs, and added legacy identity fields to the
service response for compatibility. The fresh hardware test of that change
reproduced the same failure (`out/projection-legacy-identity.log`); it did not
resolve projection. Phone-side ADB diagnostics are the next step. A previous
accessory session must be reset by reconnecting the cable
before another version exchange; reopening the same interface is insufficient.

## 2026-09-17: real accessory mode transition succeeded

Implemented AOA SEND_STRING (52) for all six identity fields and START (53),
bounded re-enumeration on the original bus/port chain, and accessory interface 0
bulk endpoint validation with claim/release. Exposed through the GUI and
`--start-accessory`; the GUI rescans after the attempt. Failed or short string
transfers stop before START. A disconnect during START alone is never considered
success; the new accessory device must actually appear and open.

Hardware run: `04E8:6860` became **`18D1:2D00`** in about 0.6 seconds. Interface 0
was claimed successfully, with bulk **IN `0x81`, OUT `0x01`**. The accessory uses
the already installed Samsung WinUSB driver `oem118.inf`; no additional driver
installation was needed. CLI exit 0, artifact `out/accessory-start.log`.
This confirms USB transport readiness only; the probe then releases the handle.
Android Auto TLS, services and video remain unimplemented.

Core tests cover the six identity transfers, null termination, short/error
responses at each field, and disconnect/error handling on START.

## 2026-09-17: successful real USB/AOA access

The approved Windows driver change was carried out on the connected Samsung.
Its parent now uses Microsoft's compatible `usbccgp` driver; interface 00 uses
WinUSB (`oem151.inf`). Original Samsung packages and device identity were backed
up before the change. No driver signing enforcement or system security setting
was disabled. Details and the prepared rollback are in `windows_connection.md`.

The existing VS2026 Debug app was run with `--probe-usb`: **exit 0**, device handle
opened, **AOA version 2 received from the real phone**. This resolves the earlier
`LIBUSB_ERROR_NOT_FOUND` blocker. Artifact: `out/winusb-probe.log`.

MTP file transfer for that interface is replaced by WinUSB. Mode switching was
subsequently implemented and verified above. Android Auto protocol negotiation,
TLS, video decoding and rendering remain unimplemented. A driver change alone
is not a completed AA connection.

## 2026-09-16: actual USB access attempt

The Samsung was tested with libusb 1.0.30. Opening `04E8:6860` fails with
`LIBUSB_ERROR_NOT_FOUND (-5)` before any AOA/TLS handshake. The current Samsung
composite/MTP/modem bindings do not expose a usable libusb handle. No driver was
changed. The full Android Auto projection objective remains unmet.

Added a selected-device USB access/AOA probe to the GUI and `--probe-usb` CLI,
RAII for libusb resources, a bounded AOA read, explicit stage/error reporting,
and libusb deployment in both build systems. Reproduced the actual failure with
the C++ app (exit 3) and an independent libusb call. Diagnostic artifact:
`out/connection-probe.log`. A successful probe would still not mean AA connected.
VS2026 Debug and Release builds passed; both GUI smoke runs exited 0, and core
tests passed. The USB-probe failure is a tested hardware-access result, not a
build or application-start failure.

The native AASDK port was investigated, not completed; compiler and dependency
obstacles and the proposed targeted WinUSB change are documented in
`windows_connection.md`. Next hardware step requires deciding on the Windows
driver change, which may affect MTP file transfer, or using an ADB development
transport. Authentication, discovery and video still require implementation.

## 2026-09-16: native Visual Studio 2026 build

- Added repository-root `HeadUnit.sln` with native `HeadUnit` and `CoreTests`
  vcxproj projects. No CMake execution or Qt VS extension is needed for this path.
- x64 Debug/Release, v145, C++20, UTF-8, shared compiler settings and automatic
  discovery of the existing local Qt 6.8.3 MSVC x64 SDK.
- Build copies the current app's Qt DLLs and Windows platform plugin into its
  output directory. F5 uses that directory as its working directory.
- Verified using VS2026 Insiders MSBuild and installed MSVC 14.51.36231:
  Debug and Release compile/link, both CoreTests runs and both real Qt/USB smoke
  runs passed (exit 0). Each scan saw 16 devices including Samsung `04E8:6860`.
- Outputs: `out/vs2026/x64/Debug` and `out/vs2026/x64/Release`.
- CMake remains an optional portability build. The native project source lists
  must be maintained alongside CMake when adding files. Current Qt classes do
  not require moc/uic/rcc; introduce those build steps if future code uses
  Q_OBJECT, .ui files or Qt resources.


## 2026-09-16: first discovery milestone

Implemented:

- C++20 CMake targets with VS2022 x64 presets and a core-only option.
- Qt6 test window with a genuine USB device tree, scan button and explicit AA-not-implemented status.
- Background USB scan through Windows SetupAPI and hub descriptor IOCTLs.
- VID/PID, manufacturer, product, serial, configurations, alternate interfaces
  and endpoints in both UI and diagnostic logs.
- Android identification evidence: vendor/name candidate, ADB interface, or AOA
  data mode; no unsupported claim of a confirmed AA-capable smartphone.
- Thread-safe UTF-8 file/console logger with UTC millisecond timestamps, explicit
  missing-descriptor diagnostics and Win32 error codes.
- Open-source research with dated revisions and component decisions.

Verification on the current Windows development machine:

| Check | Observed result |
| --- | --- |
| Configure | PASS, CMake 3.31.6-msvc6, VS2022 MSVC 19.44.35222.0, Windows SDK 10.0.26100.0 |
| Debug x64 build | PASS, Qt 6.8.3 MSVC2022 x64 |
| CTest | PASS, 1 executable covers malformed/truncated descriptors, parsing, candidate/ADB/AOA evidence and endpoint selection |
| Real console USB scan | PASS, exit 0; 15 devices, 47 interface descriptors, 40 endpoint descriptors, 0 scan errors |
| Qt runtime deployment | Completed; local runtime starts |
| Qt GUI smoke run | Window initialized, real scan completed, result tree populated, event loop stopped; empty stderr |
| Smartphone recognition on hardware | NOT VERIFIED: no Android candidate present in the observed scan |
| Linux/Pi and Windows 10 | NOT RUN; current host reports Windows build 26200 |
| Android Auto handshake/video | NOT IMPLEMENTED in this milestone |

Local artifacts (ignored): `out/usb-scan.log`, `out/ui-smoke.log`,
`out/ui-smoke-error.log`, `headunit.log`; app in `out/install/bin/HeadUnit.exe`.

Known issues/limits:

- Device `1532:0226` returned Win32 31 for its serial string. Hub `2109:0813`
  returned Win32 31 for language/manufacturer/product strings. Remaining device
  and endpoint data was retained; no dummy strings were supplied as real values.
- MSBuild printed a missing `pwsh.exe` message during post-build processing on
  this machine; the actual compile/link succeeded. This message is not from a
  project-defined PowerShell build command (there are none).
- Minimal qtbase-only installation causes deployment warnings about missing
  translations, dxcompiler/dxil and VCINSTALLDIR. The diagnostic Widgets app
  starts locally. Deployment to a clean PC, especially Debug CRT availability,
  is not verified; use a proper Release runtime package for distribution.
- No automatic hotplug, transport open, AOA probe/mode switch, authentication,
  service discovery, video, touch or audio yet.
- USB vendor/string heuristics can miss a phone or flag a non-phone. ADB/AOA
  descriptors also do not distinguish smartphones from other Android devices.
- Synchronous hub requests have no application-level timeout; closing waits for
  an outstanding scan. Handle cancellation before implementing persistent sessions.

## Smartphone test record

| Phone | Android version | Android Auto version | USB VID/PID before/after AOA | Driver | Result |
| --- | --- | --- | --- | --- | --- |
| Samsung phone (model not independently verified) | Unknown | Unknown; user reports enabled | `04E8:6860` / no AOA switch attempted | `dg_ssudbus` composite, `WUDFWpdMtp` MTP, `Modem` | USB discovery verified on 2026-09-16; AA session not attempted |

Observed non-phone VID/PID values:
`1532:0064`, `1532:0531`, `1532:0226`, `045E:0B12`, `1532:0522`,
`0B05:1939`, `05E3:0608`, `1532:0517`, `1532:0F3C`, `1532:0F1F`,
`1532:0F17`, `1532:0C02`, `1532:00A4`, `2109:2813`, `2109:0813`.
No observed VID/PID has been presented as a tested smartphone.

## 2026-09-16 18:27 UTC: phone connected

After the user connected their phone with data transfer and Android Auto enabled,
a new real scan found 16 devices (exit 0, zero scan errors). The new device reports
manufacturer `SAMSUNG`, product `SAMSUNG_Android`, VID/PID `04E8:6860`, active
configuration 1. Its descriptors were read without warnings:

- Interface 0: `06/01/01`, bulk IN `81`, bulk OUT `01`, interrupt IN `82`.
- Interface 1: `02/02/01`, interrupt IN `84`.
- Interface 2: `0A/00/00`, bulk IN `83`, bulk OUT `02`.

Windows PnP reports the MTP, modem and composite nodes as OK. Bound services are
recorded in the table above. No ADB descriptor or AOA data-mode PID was observed.
The app identifies this device as an Android candidate based on its vendor/name;
the user's connection report corroborates that it is the intended phone.
Android and Android Auto versions cannot be established from this scan.
Existing warnings concerned other peripherals, not the phone.

Log artifact: `out/phone-scan.log` (ignored, includes serial numbers). This verifies
phone discovery only. Enabling AA on the phone does not establish a protocol
session in our app, whose transport/handshake is still unimplemented. Next work
is the native protocol adapter and selected-device AOA/transport access, with
the currently bound Samsung/MTP drivers accounted for.

Next steps:

1. Attach the target phone, rescan and complete the hardware record above.
2. Verify native MSVC build of the pinned AASDK candidate and establish the
   Windows libusb/WinUSB transport for that phone/interface.
3. Implement and test AOA re-enumeration and the actual protocol lifecycle,
   following `android_auto.md`, with state-specific failures and timeouts.
4. Receive/decode/display real negotiated video. The overall projection goal
   remains open until a phone's genuine AA screen appears in our Qt window.
