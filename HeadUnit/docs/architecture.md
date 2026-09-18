# Architecture

Windows development now has a native VS2026 entry point at `../HeadUnit.sln`
(relative to HeadUnit/). It builds the same sources directly with MSBuild; the
application includes the core and USB sources, while CoreTests builds just the
portable sources. Shared settings live in `msbuild/`. CMake targets below remain
available independently for portability.

Discovery, AOA control, a persistent protocol session and the video pipeline are
implemented. Hardware verification currently reaches TLS and service discovery;
video remains unverified. Audio and touch transmission are not implemented.

```text
main (composition/lifetime)
  +-- Logger (console + UTF-8 file, mutex, UTC timestamps)
  +-- WindowsUsbBackend : IUsbBackend
  |     +-- SetupAPI: enumerate present hubs
  |     +-- Windows USB hub IOCTLs: descriptors
  |     +-- platform-neutral descriptor parser
  +-- MainWindow (Qt Widgets)
        +-- QtConcurrent worker -> IUsbBackend -> UsbScanResult
        +-- AndroidDeviceDetector -> evidence/reason
        +-- ConnectAndroidAuto -> AOA switch and claimed libusb interface
              +-- ProjectionTransport : AASDK ITransport
              +-- AndroidAutoSession -> AASDK framing/TLS/channels
              +-- VideoDecoder (FFmpeg H.264 -> RGB)
              +-- latest-frame mailbox -> Qt timer -> QLabel
```

`headunit_core` has no Qt or Windows headers. Plain C++ value types carry device
data and errors. `headunit_usb` is the Windows adapter. `HeadUnit` composes both
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

FFmpeg accepts only the advertised H.264 video path. Decoded RGB frames replace
the previous mailbox frame under a mutex; a 33 ms Qt timer consumes the newest
one, preventing a growing GUI event backlog. Only the first displayed real frame
produces the video-ready log. The hardware smoke test requires ten displayed
frames, rather than treating USB/TLS readiness as projection success.

Linux migration: replace discovery with a libusb backend; reuse data structures,
detector, tests and Qt UI. The planned libusb transport can support both platforms,
but Windows needs appropriate interface driver bindings. Raspberry Pi graphics
and codec acceleration are separate decoder/renderer decisions. Core can already
be built without Qt/Windows using `-DHEADUNIT_BUILD_APP=OFF`; a Linux build has not
been executed in this workspace.
