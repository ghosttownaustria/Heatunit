# Architecture

Windows development has a native VS2026 entry point at `../HeadUnit.sln`
(relative to HeadUnit/). It builds the same sources directly with MSBuild; the
application includes the core and USB sources, while CoreTests builds just the
portable sources. Shared settings live in `msbuild/`. `CMakeLists.txt` builds the very same
sources on Windows (VS generator) and on Linux (Ninja, system libraries, see
[linux.md](linux.md)); there is one code base, not a Windows and a Linux project. What differs per
platform is listed under [Platform layer](#platform-layer).

Discovery, AOA control, a persistent protocol session and the video pipeline are
implemented and hardware-verified with a Samsung phone: projected video, touch,
rotary/key input and audio playback all work. Microphone capture is not implemented.

```text
main (composition/lifetime)
  +-- Logger (console + UTF-8 file, mutex, UTC timestamps)
  +-- CreateUsbBackend() -> IUsbBackend
  |     +-- Windows: WindowsUsbBackend
  |     |     +-- SetupAPI: enumerate present hubs
  |     |     +-- Windows USB hub IOCTLs: descriptors
  |     |     +-- platform-neutral descriptor parser
  |     +-- Linux: LibusbUsbBackend (libusb descriptors, strings from sysfs)
  +-- MainWindow (Qt Widgets)
        +-- QtConcurrent worker -> IUsbBackend -> UsbScanResult
        +-- AndroidDeviceDetector -> evidence/reason
        +-- ConnectAndroidAuto -> AOA switch and claimed libusb interface
              +-- ProjectionTransport : AASDK ITransport
              +-- AndroidAutoSession -> AASDK framing/TLS/channels
              +-- VideoDecoder (FFmpeg H.264 -> RGB)
              +-- latest-frame mailbox -> Qt timer -> VideoWidget
        +-- QStackedWidget: VideoWidget (phone) or HomeMenu (radio), chosen by ConsoleController
```

`headunit_core` has no Qt or Windows headers. Plain C++ value types carry device
data and errors. `headunit_usb` is the adapter to the operating system (discovery, the libusb
session, driver repair, audio output). `HeadUnit` composes both
with Qt Widgets/Concurrent. USB work runs outside the GUI thread; only one scan
runs at a time. Closing waits for the current scan before destroying its backend
and logger. Windows handles and SetupAPI device sets have RAII deleters.

`IUsbBackend` deliberately describes discovery only. A later `IUsbTransport` must
represent device selection, control/bulk transfer results with actual byte counts,
partial reads/writes, timeout, cancellation and disconnect errors. A boolean-only
Receive API would lose essential framing information. Descriptor visibility does
not grant endpoint access.

AndroidAutoSession owns negotiation and channel lifecycle, using AASDK for
framing/TLS/protobuf. It includes neither Qt nor Win32. ProjectionTransport owns
a single read worker and borrows the claimed libusb interface. It buffers short
reads, preserves partial writes, uses 100 ms read polls and a bounded write
deadline. Stopping joins the reader before interface release and drains protocol
completions before destroying the Asio context.

Session lifecycle: a 100 ms tick drives all timing. A user stop (atomic flag) or a
phone-side ByeBye leads to `End()`. After TLS a user stop first sends
`ByeByeRequest(USER_SELECTION)` and waits up to 2 s for the response; before TLS it
ends immediately. `End()` stops the transport (idempotent, joins both USB workers,
later receive/send calls are rejected with OPERATION_ABORTED rather than dropped),
stops the messenger and lets `io.run()` deliver the rejections, which releases the
promise/handler ownership cycles. `RunAndroidAutoSession` logs a warning if the
session object survives that. Exceptions from handlers end the session but never
skip queued completions. Watchdogs: 30 s without any inbound data after service
discovery, and a 90 s startup limit whose message names the stage that stalled.
Undecodable video packets are dropped and still acknowledged.

UI lifecycle: `MainWindow` has a single state (Idle, Connecting,
Stopping) behind one button that connects (all steps run in `RunAutoConnect`) or ends the session. Closing the window during a
session requests a stop and closes once the worker has finished.

FFmpeg accepts only the advertised H.264 video path. Decoded RGB frames replace
the previous mailbox frame under a mutex; a 33 ms Qt timer consumes the newest
one, preventing a growing GUI event backlog. Only the first displayed real frame
produces the video-ready log. The hardware smoke test requires ten displayed
frames, rather than treating USB/TLS readiness as projection success.

## Platform layer

Only three things need the operating system, and each hides behind a small interface that
`main`/`MainWindow`/`AutoConnect` use without knowing the platform. Everything else (session, transport,
AASDK, decoder, Qt UI, detector, tests) is shared and contains no platform `#ifdef`s beyond error texts.

| Concern | Interface | Windows | Linux |
| --- | --- | --- | --- |
| USB discovery (device list, descriptors, strings) | `IUsbBackend`, `CreateUsbBackend()` | `WindowsUsbBackend`: SetupAPI + hub IOCTLs. Reads the strings of a phone that is bound to another vendor's driver | `LibusbUsbBackend`: descriptors from libusb's cache, strings from sysfs (no device access needed), otherwise from an opened handle |
| What the OS needs before libusb may open the phone | `RepairPhoneDriverNow/Elevated`, `RecoverPhoneNow/Elevated` (`DriverRepair.h`) | `DriverRepair.cpp`: rebind WinUSB, restart the device node, both elevated through UAC | `DriverRepairLinux.cpp`: nothing to repair; detects a missing udev rule (`RepairOutcome::AccessDenied`) and restarts the USB link by libusb reset or sysfs `authorized` |
| Sound output | `IAudioEngine`, `CreateAudioEngine()` | `WasapiAudioEngine` (shared mode) | `MiniaudioEngine`: PulseAudio/PipeWire, ALSA, JACK, chosen at run time |

`LibusbUsbBackend` compiles everywhere (Windows too, selectable with `HEADUNIT_USB_BACKEND=libusb`), as does
`MiniaudioEngine` (`-DHEADUNIT_AUDIO_BACKEND=miniaudio`), which keeps the Linux paths testable on the development
machine. The build selects the rest by file: `CMakeLists.txt` adds `WindowsUsbBackend.cpp`/`DriverRepair.cpp`/
`WasapiAudioEngine.cpp` on Windows and `DriverRepairLinux.cpp`/`MiniaudioEngine.cpp`/`MiniaudioImpl.cpp` elsewhere;
`HeadUnit.vcxproj` lists the Windows ones (the Linux ones are `None` items). The choice per call happens in the two
factories (`UsbBackendFactory.cpp`, `AudioEngineFactory.cpp`).

The libusb session is the same everywhere. What it needs from the platform is small: on Linux the open of an
accessory device is retried until the device node gets its access rights (udev applies them a moment after the
node appears), and a kernel driver that holds the accessory interface is detached while it is claimed
(`libusb_set_auto_detach_kernel_driver`, a no-op on Windows). `AndroidUsbProbe.cpp` keeps the platform specific advice in
`AdviseOnOpenFailure`: on Windows a failed open means a wrong driver and `canRepairDriver` starts the repair; on
Linux it means missing access rights, which only an administrator can grant, so it is reported and never repaired.
`AutoConnectDeps::needsAdminPrompt` tells the flow whether its step messages should mention Windows' administrator
prompt.

Other platform helpers: `platform/Environment.h` (`GetEnv`, MSVC deprecates `getenv`), `DefaultLogPath` (the working
directory when writable, otherwise the user's state directory). Raspberry Pi graphics and codec acceleration
remain separate decoder/renderer decisions; FFmpeg's software H.264 decoder is used everywhere. The core builds
without Qt or any library using `-DHEADUNIT_BUILD_APP=OFF`.

Wireless Android Auto (Linux only, `src/wireless/`, built by the `headunit_wireless` target when Qt6 DBus is found;
[wireless.md](wireless.md)) adds a second `ITransport` and a connection flow, and leaves the session untouched:
`ConnectWirelessAndroidAuto` switches Bluetooth on (rfkill unblock, adapter power), makes the computer visible and
registers the Android Auto Wireless service with BlueZ (`BluetoothService`, QtDBus). Only when a phone opens that service
does it start the NetworkManager hotspot (`Hotspot`, `nmcli` without a shell, on its own thread while Bluetooth keeps
being answered), then `EstablishWirelessLink` hands the phone the Wi-Fi details over the RFCOMM socket
(`WirelessHandshake`: pure message logic; both Qt-free and tested against a simulated phone) and accepts the phone's TCP
connection on port 5288, and `RunAndroidAutoSession` runs on a `SocketTransport` (the TCP twin of
`ProjectionTransport`: one reader, one writer, `stop()` rejects with OPERATION_ABORTED). The hotspot goes down after
every attempt. It runs on the same worker thread as the USB flow; QtDBus needs that thread to run Qt events, which
`BluetoothService::WaitForPhone`/`Pump` do. Only
this target uses moc (`AUTOMOC`), and `HEADUNIT_WIRELESS` is defined only where it is built, so the Windows build and
`MainWindow` without the wireless button are unchanged.

Input and audio: the window owns a `ProjectionInput` (GUI thread -> protocol thread) and an
`AudioState` plus an `IAudioEngine`. `ProjectionCallbacks` hands them to the session, which attaches
to the input bus while it runs and turns `InputEvent`s into `InputReport` messages on its strand
(dropped until the phone has opened the input channel). The audio sinks write PCM into
`IPcmOutput`s opened lazily on the first sample; each WASAPI stream has its own render thread and a
bounded ring buffer, applies the shared volume/mute gain and reports levels back for the display
(WASAPI polls the device from that thread; miniaudio pulls from the ring in its callback, and its stream opens the
device on its own thread so a sound server that hangs cannot stall the protocol thread).
`CarPanel` (rotary knob, keys, audio display) and `VideoWidget` (picture and touch mapping) are plain
Qt widgets without moc; the portable parts (touch mapping, input bus, PCM helpers) are header-only
and unit-tested in CoreTests.

Display size: `DisplayConfig` (portable) is the one place that knows the offered sizes (800x480, 1280x720,
1600x600, 1920x1080), the fixed Android Auto resolutions that carry them, the density and how to parse/print
them. `VideoLayoutOf` maps a display to a `VideoLayout`: the frame the phone encodes (smallest fixed
resolution that holds the display), the margins that fit the display's shape into it, and the shown area.
`MainWindow` holds the chosen size (combo box, only enabled while idle, remembered with `QSettings`,
`--display` override for a run) and hands it to the session in `ProjectionCallbacks::display`;
`DisplayService.h` turns it into the video service's `VideoConfiguration` (resolution, margins, density), and
the session announces the shown area as the touchscreen. Video and touch therefore share one coordinate
space: the pixels of the shown area, which `VideoWidget::SetFrame` cuts out of each decoded frame (the phone
draws only there and leaves the margins black). The size cannot change while a session runs. The window passes
it on to `VideoWidget` (crop, touch mapping, shape of the empty screen), `HomeMenu` (shape and scale of the
menu) and `ConsoleController` (where Home taps). `DetectPhoneScreen` and `DashboardButtonPosition` scale the 800x480 layout by `shown height / 480`,
anchored at the left and bottom edges of the shown area, because the density keeps the phone's layout
height constant.

Panel layout: `CarPanel` has the controller keys (MEDIA, TEL, NAV, the projection key, HOME, BACK) and the round
`RotaryKnob` at the top and all other keys plus the audio display in a separate section below. The knob has five
click zones (four arrows on the rim, the push button in the middle), classified by the portable `KnobZoneAt`
in `ui/KnobZones.h`; dragging around it and the mouse wheel turn it.

Hard keys: `CarPanel` reports controller keys (`ConsoleKey`: Home, Menu, Option, Media, Radio, Tel,
Nav, Map, Back, Projection) to `MainWindow::PressConsole`, which asks the portable `ConsoleController`
what the key means and gets a `ConsoleEffect` back: car keys and touch taps for the phone, one line for
the window log, and whether to connect first. The radio side (Radio, Menu, the second step of Home) is the
home menu, see below; there is no radio operating system behind it yet. Home is two-step while a phone is projected;
where the phone currently is comes from `DetectPhoneScreen`, which reads the symbol of the navigation
bar's bottom-left button from the newest decoded frame (the protocol never reports the phone's screen,
and `KEYCODE_HOME` only opens the app launcher). `ApplyConsoleEffect` sends keys and taps through
`ProjectionInput` and re-reads the picture once after the phone's home key when the picture was
not readable. Hardware tests for this are `--test-console` and the diagnostic `--test-keys`.

Home menu: the radio's own screen, drawn after the design `docs/design/home-menu.svg` (clock, six tiles:
Multimedia, Radio, Telephone, Navigation, Vehicle, Settings; the focused tile's stripes and symbol turn orange).
`MainWindow` keeps `VideoWidget` and `HomeMenu` in a `QStackedWidget`; `ShowScreen` puts the menu in front whenever
`ConsoleController::CurrentScreen()` is `RadioHome`, which is always the case without a projection (before the first
frame, while connecting, after the session). The decoded frames keep flowing into the hidden `VideoWidget`, so Home can
still read the phone's picture. The layout is portable (`ui/HomeMenuLayout.h`, in CoreTests): design units 600 high and
as wide as the display's shape, tile positions, the scroll that keeps the focused tile in view with the least movement,
hit testing and which `ConsoleKey` a tile stands for (Multimedia Media, Radio Radio, Telephone Tel, Navigation Nav;
Vehicle and Settings only log). `HomeMenu` draws the design's own outlines (its SVG path data, parsed once into
`QPainterPath`s; no QtSvg dependency) and gradients (with their `gradientTransform` as brush transform), slides the row
with a short `QVariantAnimation` and opens a clicked tile on release. While the menu is in front the knob works it:
`MainWindow::SendKey` sends left/right/push to the menu (up/down are swallowed, the menu is one row) and everything else
to the phone; a key's release always goes where its press went, so neither side sees half a key press. Turning moves
the focus instead of the phone's.
