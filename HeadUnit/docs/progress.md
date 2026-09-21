# Progress

## 2026-09-21: one code base for Windows and Linux

Goal: the same C++ sources build and run reliably on Windows and Linux, not two projects. The survey showed that
the code was already portable except for three operating system concerns, so the work was to put those behind
interfaces and to make the build portable. Details: [architecture](architecture.md#platform-layer), [linux](linux.md).

- **USB discovery:** `CreateUsbBackend()` returns `WindowsUsbBackend` on Windows (unchanged behaviour) and the new
  `LibusbUsbBackend` elsewhere: libusb's cached descriptors, strings from sysfs so that a phone that cannot be opened
  yet still shows its name and serial number. Shared device logging moved to `UsbLogging.cpp`.
- **Driver access:** `DriverRepair.h` keeps its contract (plus `RepairOutcome::AccessDenied`); `DriverRepair.cpp` stays
  the Windows implementation, `DriverRepairLinux.cpp` detects a missing udev rule (which only an administrator can
  add: `scripts/install-udev-rules.sh`, `packaging/linux/70-headunit-android.rules`) and restarts the USB link on a
  best-effort basis (libusb reset, then sysfs `authorized` for root). `AndroidUsbProbe.cpp` gives per-platform advice,
  never sets `canRepairDriver` on Linux and detaches kernel drivers from the accessory interface.
- **Audio:** `IAudioEngine` + `CreateAudioEngine()`; `WasapiAudioEngine` (only its header changed) on Windows,
  `MiniaudioEngine` (vendored miniaudio 0.11.25, PulseAudio/PipeWire, ALSA, JACK chosen at run time, device opened on
  the stream's own thread) elsewhere. New phone-independent check `HeadUnit --test-tone`.
- **Smaller portability fixes:** `sscanf_s` -> `std::from_chars`; `getenv` -> `GetEnv` (also removes a C4996
  warning); the log file falls back to the user's state directory when the working directory is read-only;
  Windows-only wording in `AutoConnect` is shown only where the administrator prompt exists
  (`AutoConnectDeps::needsAdminPrompt`, with a unit test); `.vscode/settings.json` no longer holds an absolute path of
  an older checkout; `.gitattributes` keeps LF in shell scripts and udev rules.
- **Build:** `CMakeLists.txt`, `cmake/Protocol.cmake` and the new `cmake/Libusb.cmake` build the same sources on both
  systems (vcpkg tree on Windows, system packages through pkg-config/`find_package` on Linux); presets `linux-debug`,
  `linux-release`, `linux-core-only` were added and the Windows presets are only offered on Windows. The MSBuild
  projects list the new sources. `.github/workflows/build.yml` builds and tests Linux fully and the portable core on
  Windows.

Verified on this Windows machine: MSBuild Debug x64 with 0 errors and no warnings from own sources; `CoreTests` (with
the new AutoConnect test) and `ProtocolTests` pass; `--scan` lists the same 15 USB devices with the same 47
interfaces and 40 endpoints through the native backend and through `HEADUNIT_USB_BACKEND=libusb`; `--test-tone`
through WASAPI plays all 384000 bytes without drops; `--smoke-test` opens the Qt window; the log falls back to
`%LOCALAPPDATA%\HeadUnit` when the working directory is read-only. The CMake path on Windows (`windows-vs2022` preset,
Visual Studio 2022 generator, built in a temporary directory) configures, builds and passes `ctest` (2 of 2). With
`-DHEADUNIT_AUDIO_BACKEND=miniaudio` (the Linux audio engine, on Windows running on top of WASAPI) `--test-tone` also
delivers all 384000 bytes, and `HEADUNIT_AUDIO_DEVICE` switches between output devices (four search words, three
different devices).

Verified for Linux by cross-compiling only (zig 0.16 / clang with libc++ against the same Boost, OpenSSL, protobuf,
FFmpeg, libusb and Qt headers): every application source and test compiles for x86-64 with `-Wall -Wextra` and no
new warnings (three sign-compare warnings in `ProtocolTests.cpp` predate this work); the libusb backend, the Linux driver
check, the USB probe, the miniaudio engine, the logger, `AutoConnect` and `main` also compile for ARM64 and ARM32; the
AASDK sources compile for x86-64; the UI sources also compile against the Qt 6.4.3 headers; all `#include` spellings
match the file names in their case.
**Not verified:** nothing has been linked or run on Linux (no Linux environment was available), so the udev rule,
sysfs string reading, the PulseAudio/ALSA paths, the CMake Linux branches and the USB restart on Linux are unproven
until the first run there; the checklist is in [linux.md](linux.md). The Windows behaviour with a real phone was not
re-tested after the refactoring (no phone connected); the Windows code paths for the phone are unchanged except for
the shared logging and the `AdviseOnOpenFailure` helper, which keeps the texts and flags.

## 2026-09-20 (last): console layout from the user's sketch

- **Layout** (`CarPanel`): top row MEDIA, TEL, NAV and the projection key as a phone-with-play symbol
  (tooltip "CarPlay / Android Auto", the old wide text button is gone), HOME at the left and BACK at the right
  below it, then a large round controller (260 px). Everything else (MENU, OPTION, RADIO, MAP, track skip and
  play/pause, Leiser/Stumm/Lauter) is in a separate section "WEITERE TASTEN" underneath, followed by the audio
  display. The panel needs about 684 px of height.
- **The round controller now carries the four arrows** (`RotaryKnob`, zones in the portable `ui/KnobZones.h`):
  an arrow drawn at each side of the disc, a click without dragging on one of them (the rim outside half the
  radius, four 90 degree sectors) sends that direction key, a click on the inner circle presses the controller
  (DPAD_CENTER), dragging around it or the mouse wheel still turns it (detents of 15 degrees, no turning right
  at the middle). A click that ends in another zone than it began is dropped; the zone under the mouse lights up
  and per-zone tooltips explain the places. The separate arrow buttons are gone. Arrows now send down and up
  together when the click ends (before: down on press, up on release), so a mouse cannot hold a direction key;
  the keyboard arrows still repeat.
- **Checked** with a scratch program that drives the real `CarPanel` with synthetic mouse and wheel events
  (not part of the project): every arrow, the middle, the outside, drag turns in both directions, wobble,
  cancelled clicks, the wheel, all buttons and their console/media keys, and the position of the sections; plus
  screenshots of the normal, hovered and pressed states. CoreTests cover the zones.

## 2026-09-20 (even later): 1600x600 display

- **New size 1600 x 600 (Ultrawide)** in the list (800x480, 1280x720, 1600x600, 1920x1080). Android Auto has no
  1600x600 resolution, so the display is fitted into a fixed one with the video configuration's margins:
  `VideoLayoutOf` picks the smallest fixed resolution that holds the display (1920x1080) and fits the display's
  shape inside it (height margin 360, width margin 0, sizes kept even). Density follows the shown height (720
  -> 240 dpi, so the phone's layout height stays 480 units).
- **Measured on the Samsung SM-F776B** (raw frames saved by `--test-keys`, with a temporary switch that has
  been removed again): with codec 1920x1080 and height margin 360 the phone still sends the full 1920x1080
  frame, draws its interface only in the centred 1920x720 strip (y 180..899) and leaves the rest black. Touch
  coordinates count in pixels of that strip: a tap at strip position (60,347) hit the Spotify icon whatever
  touchscreen size was advertised, while the same icon addressed with frame coordinates (60,527) hit the
  microphone icon (and opened the Google Assistant on the phone for a moment; no microphone is connected).
  So the app advertises the strip as the touchscreen, crops each frame to the strip (`VideoWidget::SetFrame`,
  so the picture, the touch mapping, the screenshots and the phone-screen reading all see the strip) and sends
  strip coordinates. The window scales the 1920x720 strip to whatever room the picture has (8:3).
- **Wide layout:** on this display the phone moves its navigation bar to a rail at the left and shows the dashboard
  as a big map card next to the media card. The button at the bottom left of the rail is at the same place as
  on 720 px high displays (63,657), shows nine dots on the dashboard and the framed symbol elsewhere, so the
  Home logic needed no change. The Nav key does not open the map as an app there (the map already is the big
  card), so `--test-console` now starts with Media (opens an app in every layout) and only reads, not judges,
  the picture after Nav at the end.
- **Verified on the phone at 1600x600:** frames 1920x1080 with the strip cropped correctly, `--test-console`
  (Home read "other" -> tapped -> read "dashboard", second Home only logged the radio menu, Media -> Home
  -> dashboard, Radio, Nav), `--test-input` (rotary, keys and a touch tap in the middle of the strip reach the
  phone).
- **Tests:** CoreTests cover the layout of every offered size and of other shapes (a taller one such as
  1024x600 gets a width margin, sizes that no frame holds and non-positive sizes get none), touch mapping onto
  the 1920x720 strip, the picture reading and the Home tap for 1600x600; ProtocolTests check the video
  configuration built for it (1920x1080, height margin 360, 240 dpi). A test found that a size of 0x0 was given
  a frame; fixed.

## 2026-09-20 (later): selectable display size

- **Feature:** while nothing is connected, a combo box next to the connect button offers 800x480, 1280x720
  (HD) and 1920x1080 (Full HD). The size is announced to the phone once, in the service discovery: the video
  resolution constant, the touchscreen size and a screen density that grows with the height
  (`160 * height / 480`, so 240 dpi at 720p and 360 dpi at 1080p), which keeps the phone's interface the same
  size in layout units at every resolution. During a connection the combo box is disabled. The choice is
  remembered per Windows user (`HKCU\Software\HeadUnit\HeadUnit`, value `display`); `--display WxH` overrides it
  for one run without saving, and all scripted phone tests ignore the remembered value (default 800x480 unless
  `--display` is given). The empty picture area shows a screen of the chosen shape and "Display W x H".
  Android Auto only has fixed resolutions; other shapes need margins in the video configuration (added
  afterwards for 1600x600, see the section above).
- **Code:** `DisplayConfig.h` (portable list, density, text/parse), `DisplayService.h` (video configuration for
  the service discovery), `MapToTouch` takes the display, `DetectPhoneScreen` and the Home tap position now scale
  with the display height (`LayoutScale`, anchored at the left/bottom edge, so wide 16:9 pictures work),
  `ConsoleController::SetDisplay`. The old `kTouchWidth/kTouchHeight` constants are gone.
- **Verified on the Samsung SM-F776B:** `--test-console` at 1280x720 (frames arrive as 1280x720; the dashboard
  button sits at 63,657 as computed; Home reads "other" then "dashboard", second Home only logs the radio menu),
  `--test-console` at 1920x1080 (frames 1920x1080, same Home sequence), `--test-input` at 1280x720 (rotary,
  keys and a touch tap in the middle of the picture reach the phone), and `--test-projection` / `--test-audio` at
  the default size. Picking a size in the real window was driven through the window's own popup: the value
  is written to the registry and loaded again at the next start.
- **Also fixed:** a phone whose USB link stopped answering (accessory device `18D1:2D00` idle for a long time;
  Windows: "device does not work", `LIBUSB_ERROR_TIMEOUT` when reading the serial number) ended the one-button
  flow with an error. That failure now counts like a silent Android Auto: the connection ladder restarts the
  phone's USB link through the elevated helper and tries again (which recovered the phone during the tests above).
- **Tests:** CoreTests cover the size list, parsing (valid and invalid texts), density, touch mapping for the
  other sizes, the picture reading and Home tap position at all three sizes; ProtocolTests cover the video
  configuration built for each size (resolution constant, density, frame rate, margins).

## 2026-09-20: BMW-style hard keys and a two-step Home

- **Keys:** the console follows a BMW iDrive multimedia controller: Media, Radio, Menu, Tel, Nav, Back,
  Option, plus Map, CarPlay/Android Auto, Home, volume up/down, mute, skip forward/back (and play/pause),
  around the rotary knob with its four nudge keys. Pressing a key goes through `ConsoleController`
  (portable, header-only, in CoreTests): Media, Tel, Nav and Map send the phone's Media, Phone and
  Navigation car keys (Map and Nav both open the phone's navigation app, there is only one key; Nav and
  Media verified on the phone), Option sends `KEYCODE_MENU` (its effect on the phone was not verified),
  Back sends `KEYCODE_BACK`. Radio and Menu
  have no function without an operating system behind them and only write a line to the window log, as
  does the radio side of Home. The CarPlay/Android Auto key starts the connection when nothing is
  connected. Keyboard: Pos1 Home, Esc/Backspace Back, F1 Menu, F2 Option, F3 Media, F4 Radio, F5 Tel,
  F6 Nav, F7 Map, F8 CarPlay/Android Auto.
- **Home is a two-step key while a phone is projected:** the first press brings the phone to its
  dashboard (map, media and phone cards), a second press while the phone shows that dashboard goes to the
  radio's home menu (log line only), a third press goes back to the phone's dashboard. Without a
  projection Home only logs the radio menu.
- **`KEYCODE_HOME` cannot be used for the dashboard:** on this phone it always opens Android Auto's
  app launcher (the 3x3 grid), and `KEYCODE_APP_SWITCH` and Back do nothing useful. The bottom-left
  button of the navigation bar (touch position 42,438) toggles: it shows a framed split-view symbol
  everywhere except on the dashboard, where tapping it goes to the dashboard, and nine dots on the
  dashboard itself, where tapping it opens the launcher. So Home **reads the phone's picture**
  (`DetectPhoneScreen`: shape of that symbol, nine small separate dots against one large connected
  frame, independent of colours and picture size; a focus ring around the button is ignored) and taps
  the button only when the phone is not on the dashboard. When the picture cannot be read (a
  transition), it sends the phone's home key and looks at the picture again after one second.
- **Verified on the Samsung SM-F776B:** `--test-console` (Nav opens Maps, Home reads "other" and returns
  to the dashboard with Maps and Spotify cards, the second Home reads "dashboard" and only logs the radio
  menu and leaves the phone alone, Media opens Spotify, Home again reads "other" and returns to the
  dashboard, Radio logs); `--test-input`, `--test-audio` and `--test-projection` still pass.
  `--test-keys` with `HEADUNIT_TEST_KEYS` (steps: a number is a car key code, `t:X:Y` a tap, `c:name` a
  console key) is the diagnostic used to find out what the phone does with a key.
- **Tests:** CoreTests now also cover the picture reading (synthetic frames: dots, frame, focus ring
  with and without symbol, black, light, stray blobs, two picture sizes) and the Home logic for all
  three picture readings.

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
