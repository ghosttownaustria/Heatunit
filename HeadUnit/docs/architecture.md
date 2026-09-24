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
        +-- QStackedWidget: VideoWidget (phone) or a MenuPage of the radio, chosen by ConsoleController
        |     +-- HomeMenu, MultimediaPage (MusicLibrary), RadioPage (RadioBrowser -> radio-browser.info)
        +-- AudioPlayer (FFmpeg avformat/avcodec/swresample, own thread) -> IAudioEngine media output
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

Radio pages: the radio's own screens, drawn after the design `docs/design/home-menu.svg`. They share `MenuPage` (a black
screen of the display's shape, the clock, painting in design units 600 high and as wide as the display's shape; the
controller as `Turn`/`Nudge`/`Push`/`Back`) and `ui/MenuStyle` (the design's font, colours, the tiles with the design's
own outlines, parsed once from its SVG path data into `QPainterPath`s with its gradients as brush transforms, no QtSvg
dependency, and the focus mark: the tiles' frame with orange corner stripes). `MainWindow` keeps `VideoWidget` and the
pages in a `QStackedWidget`; `ShowScreen` puts the page that `ConsoleController::CurrentScreen()` names in front:
`RadioHome` the home menu, `Multimedia` and `Radio` the player pages. Without a projection the screen is always one of
these (before the first frame, while connecting, after the session). The decoded frames keep flowing into the hidden
`VideoWidget`, so Home can still read the phone's picture. The console decides the pages: Menu and Home's second step
give the home menu, Radio the tuner, Media without a phone the music player; Home and Back on a player page lead to the
home menu (Back first goes to the page, which may close something it opened, the tuner's country list).

- `HomeMenu`: the row of tiles the user chose (Android Auto, Multimedia, Radio, Telephone, Navigation, Vehicle,
  Settings; the focused one orange), sliding with a short `QVariantAnimation`; an edge beyond which more tiles follow
  is a black strip with a fade, a line and an orange arrow; the bar at the bottom shows which part of the row is in
  view. Turning moves the focus, left/right move the focused tile along the row (`onShift`, the window stores the
  order), the wheel turns, a click on an edge arrow moves the focus. The portable `ui/HomeMenuLayout.h` (CoreTests) has
  the tile positions, the scroll that keeps the focused tile in the middle (clamped at both ends of the row), the edges
  and the bar, hit testing, what a tile opens (`HomeMenuPage`: Multimedia, Radio, Settings; `HomeMenuKey`: Android
  Auto Projection, Telephone Tel, Navigation Nav; Vehicle only logs) and `HomeTileSetup`: every tile once in the
  user's order, shown or hidden (Settings always shown), `MoveTile` (on the menu past the next shown tile, in the
  settings list past the direct neighbour) and a text form kept in `QSettings` (`home/tiles`, e.g.
  `AndroidAuto,Radio,-Vehicle,...`; damaged or older texts are repaired, new tiles appended).
- `SettingsPage`: every tile with a tick box in the menu's order; turning chooses, pushing shows or hides, up/down move
  the tile along the order.
- `PlayerPage` (`ui/MediaPages`): the page's tile at the left (orange while its source sounds), what plays now, the
  controls previous/play/next, a list of five rows and buttons at the top right. The focus moves through them by the
  portable `PageFocus` rules in `HomeMenuLayout.h` (tested): turning moves within a part (list rows, a row of buttons),
  up/down jump between the parts from wherever the focus is, left/right never move the focus but skip to the previous
  or next title or station (not while the country list is open); the list scrolls by `ListFirstRow`. The page polls
  the player four times a second.
- `MultimediaPage`: the music folder (`HEADUNIT_MUSIC_DIR`, else `HeadUnit` in `QStandardPaths::MusicLocation`, created
  when missing), read in the background by the portable `media/MusicLibrary.h` (`std::filesystem`, recursive, natural
  order, the folder first, then each sub folder; tested with a real temporary folder), again whenever the page is shown.
  At the end of a title it plays the next one; a file that fails is skipped, but not endlessly.
- `RadioPage`: the stations of one country from radio-browser.info (`radio/RadioBrowser`, QtNetwork with its Schannel
  TLS backend on Windows, JSON by QJsonDocument; the servers `de1`, `de2`, `all` in turn; working stations of the country
  the 500 most listened, shown alphabetically by `QCollator` (case ignored, numbers by value, as
  is the country list); a click is reported to the directory as it asks). The country defaults to
  `QLocale::system()`'s territory; country and last station are kept in `QSettings` (`radio/country`, `radio/station`).
  Real DAB+ reception would need a tuner; a DAB+ source (for example `welle-cli` with an RTL-SDR stick, which serves the
  programmes as http streams) would plug in as another list of stream URLs.

Knob routing: while a page is in front, `MainWindow::SendKey` gives the controller's arrows and push to it
(`PressLocally`); the media keys (play/pause, track skip, from the panel or the keyboard) go to the radio's own player
while it plays or is paused (the page that started it handles them), otherwise to the phone. A key's release always goes
where its press went (`m_localKeys`), so neither side sees half a key press; held arrows keep nudging the side that took
the press. Turning goes to the page instead of the phone (keyboard: comma and period turn, the arrow keys are the
controller's arrows).

The radio's own player: `media/AudioPlayer` decodes a file path or an http(s) URL with FFmpeg (avformat for the
container and the network, including ICY "now playing" titles read from the http context, avcodec, swresample to 48 kHz
stereo 16-bit) and writes it to a media output of the same `IAudioEngine` as the phone's music, so volume, mute and the
level display apply. It runs one worker thread for its whole life; `Play`, `SetPaused` and `Stop` only hand over a
request, and every blocking FFmpeg call checks an interrupt callback, so changing the station never blocks the GUI
thread. The output is paced by `IPcmOutput::Queued()` (new): at most 250 ms decoded ahead in the 500 ms ring, so a file
plays in real time and pause and stop answer at once. Status (state, tags, stream title, position, duration, error)
carries a generation that counts `Play` calls: a page owns the player while the generation is the one its own `Play`
got, and a finished run cannot overwrite a newer one's status. Streams use FFmpeg's reconnect options and a 15 s
read timeout; a live stream that ends or drops is reported, not retried.

One sound at a time (`audio/AudioFocus.h`, tested): the source started last keeps the media sound. When the radio's
player starts or resumes, the window notes the time and sends the phone `MediaPause`. The phone's media output is
wrapped in a `WatchedOutput` (the opener handed to the session), which tells a `MediaActivity` about every block: audio
after a gap of more than 500 ms counts as a new start. `MainWindow::Tick` asks `IsPhoneTakingOver`: when the phone
plays and started after the radio's player, the pages give way (`GiveWay`: the music pauses, the radio stops). The
phone's music that runs out after its pause key started earlier and does not count.
