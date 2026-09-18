# Windows USB connection

## 2026-09-18: WinUSB binding reverts whenever the phone re-appears in file-transfer mode

Symptom: `USB open: LIBUSB_ERROR_NOT_FOUND (-5)` although the phone is attached and in
file-transfer mode. Evidence (`C:\Windows\INF\setupapi.dev.log`, Kernel-PnP event 442
"device settings not migrated ... partial or ambiguous match"): every time the phone
re-appears as `04E8:6860` (cable replug, phone returning from accessory mode) Windows
treats it as a new installation and picks the best-ranked driver, which is Samsung's
`ssudbus` (`oem144.inf`, rank 00FF0001) over the composite driver that was forced onto
the previous device node. `oem151.inf` (libwdi, WinUSB for `MI_00`) only takes effect
while the composite parent is `usbccgp`, so the binding is lost with each re-appearance.

Three changes make this a non-issue in daily use:

1. **The phone no longer has to leave accessory mode between sessions.** After a session
   the phone stays in accessory mode (`18D1:2D00`, Samsung's own `ssudaoa` WinUSB driver,
   which Windows keeps). Connecting to a phone that is already in accessory mode now
   re-sends the AOA identity and START, which makes Android launch Android Auto afresh
   (verified on the Samsung SM-F776B: three consecutive sessions with real video and a
   clean stop each, no replug). So the file-transfer-mode driver only matters after a
   cable replug or phone reboot.
2. **Self-repair in the window.** If opening the phone in file-transfer mode fails with a
   driver error, the window runs `HeadUnit.exe --repair-driver` elevated (Windows shows
   its administrator prompt), which switches the composite parent to Microsoft's
   `usbccgp` and waits until `MI_00` reports WinUSB, then rescans and continues the
   connect on its own. No script is needed. Manual use, same effect:

```powershell
HeadUnit.exe --repair-driver
```

3. **Restart for a phone that does not answer.** `HeadUnit.exe --recover-phone` (started by the
   window when the phone stays silent) disables and re-enables the phone's accessory device node
   in one elevated process. The phone sees the bus reset, leaves accessory mode and returns as
   `04E8:6860` like after a cable replug (verified). The same process then repairs the driver.
   After a restart the phone shows a single MTP interface; the composite driver is then not in
   the compatible list, so it is picked from the USB class list with `DI_FLAGSEX_ALLOWEXCLUDEDDRVS`
   (hardware ID `USB\COMPOSITE`), and `MI_00` gets the existing WinUSB package. libusb's
   `libusb_reset_device` and `IOCTL_USB_HUB_CYCLE_PORT` do not restart the phone on this machine.

Exit codes: 0 fixed, 1 already fine, 2 no phone, 3 phone in accessory mode (nothing to
do), 4 phone not in `04E8:6860` layout (set the phone to file transfer), 5 install
failed, 6 timeout, 7 not elevated, 9 UAC declined. Details: `headunit-repair.log`.

Open point, "charging only" like a real car: Android Auto starts through AOA, which
also works while the phone is in "no data transfer" mode. The missing piece is a WinUSB
binding for that mode, because its PID/interface layout is not known yet (the log also
shows `04E8:6863`, the tethering/RNDIS layout). Set the phone to "No data transfer",
scan and record VID/PID and interfaces here. A permanent fix independent of Windows'
re-selection needs the Samsung parent package (`oem144.inf`, `oem64.inf`) to be removed
from the driver store (backup in `.tools/usb-driver-backup/original-samsung`), which
also affects other Samsung tools; that has not been done.

## 2026-09-17: USB driver blocker resolved on the test phone

With the user's approval, the connected Samsung `04E8:6860` was changed as follows:

| Device/function | Before | Now |
| --- | --- | --- |
| Composite parent | Samsung `dg_ssudbus`, `oem144.inf` | Microsoft `usbccgp`, `usb.inf` |
| Interface 00 (MTP) | `WUDFWpdMtp`, `wpdmtp.inf` | `WinUSB`, `oem151.inf` |
| Interface 01 (serial/modem) | Samsung modem under proprietary parent | Microsoft `usbser` under the standard composite parent |

The proprietary Samsung parent exposed nonstandard child IDs which Zadig could
not identify safely for interface-only replacement. The compatible Microsoft
composite driver was therefore selected for this specific phone first. After
that, standard `MI_00`/`MI_01` children appeared. Only `MI_00` received WinUSB;
the modem and other peripherals were not selected for WinUSB installation.
Windows reported that no reboot was required for the parent change.

**Verified result:** the real C++ app's `--probe-usb` run exited 0. `libusb_open`
succeeded and control request 51 returned **AOA version 2**. Evidence is in
`out/winusb-probe.log`. This confirms live control transfers to the phone; no
AOA mode switch, Android Auto TLS session or video stream has been attempted yet.

Driver artifacts and the exact pre-change instance IDs are saved locally in
`../.tools/usb-driver-backup/` (relative to HeadUnit). The original Samsung parent
and modem packages were exported before changing drivers. The existing Windows
MTP package was not deleted. Explorer file transfer on interface 00 is currently
replaced by WinUSB.

To restore the original Samsung arrangement, run an Administrator PowerShell at
the repository root while the same phone is attached in this USB mode:

```powershell
& .\.tools\usb-driver-backup\parent-driver.ps1 -Install -Restore
```

The local helper verifies the recorded phone instance and selects the original
`oem144.inf` from its compatible driver list. This rollback path is prepared but
has not been executed after the successful probe. The exported parent package is
also retained in `.tools/usb-driver-backup/original-samsung/ssudbus.inf`. Do not
delete driver-store packages as a recovery shortcut. A later accessory-mode
VID/PID will have its own driver binding and needs separate verification.

## Original failure (2026-09-16)

2026-09-16, Samsung `04E8:6860`: discovery succeeds, but an actual
`libusb_open` returns `LIBUSB_ERROR_NOT_FOUND (-5)`. Reproduced with the
application's `--probe-usb` path and independently with libusb 1.0.30.
Windows PnP reports `dg_ssudbus` for the composite parent, `WUDFWpdMtp` for the
MTP function and `Modem` for the modem function. These are not WinUSB bindings.

This is a USB-access failure before the AOA query, not a failed TLS handshake.
No AA session, video, or device mode change occurred. Installing a suitable
driver would only remove this first blocker: our AA protocol integration and
decoder still have to be implemented.

## Reproduce without changing the system

Open `HeadUnit.sln`, build, run, select the Samsung and click
**Android Auto: USB-Zugriff pruefen**. The result includes the failing stage and
libusb error name. Alternatively run `HeadUnit.exe --probe-usb`; it requires
exactly one Android candidate and exits 3 on failure. A successful probe only
means that USB/AOA access works, not that AA is connected.

The probe opens only the selected VID/PID, refuses ambiguous matches and checks
the serial if available. In normal mode it sends the two-byte AOA GET_PROTOCOL
read (51), with a 2-second timeout. In accessory mode it validates the bulk pair
on interface 0 and briefly claims/releases it. The probe never switches modes
or installs drivers.

The separate **Android Auto: Accessory-Modus starten** action (CLI:
`--start-accessory`) sends all six identity strings (52), then START (53).
It waits up to 15 seconds for the new accessory VID/PID at the same physical
port and validates the bulk interface. On 2026-09-17 the real phone switched
from `04E8:6860` to `18D1:2D00`; the existing Samsung AOA WinUSB driver
`oem118.inf` allowed interface 0 to be claimed (IN `0x81`, OUT `0x01`).
An already-accessory-mode phone skips the switch and just checks the interface.
The check releases its handle afterwards; AA authentication/video do not run yet.

## Proposed next hardware step

Use WinUSB for the selected Samsung USB function after agreeing to the driver
change. [libusb's Windows documentation](https://github.com/libusb/libusb/wiki/Windows)
describes the driver requirement and recommends [Zadig](https://zadig.akeo.ie/).
Target identification for this phone's currently observed configuration:

- VID/PID `04E8:6860`; MTP function/interface 0, class `06/01/01`.
- Do not choose the modem function, a hub, another peripheral, or replace every
  device with the same vendor ID. Verify the exact device/interface shown before
  installing. With the Samsung composite parent, interface-only replacement may
  not suffice; reassess access before considering any parent-driver replacement.
- A WinUSB replacement on the MTP function can disable Explorer file transfer
  until the original driver is restored. Record/export the original driver
  package and device instance before changing it; do not delete OEM packages.
- After an approved change: rescan, rerun the probe and record its actual result.
  A later AOA mode switch changes VID/PID and may require a separate WinUSB binding
  for the new accessory interface. This phone already has a working accessory
  binding, as verified above.

The proposed change above was performed on 2026-09-17 as recorded at the top.
Alternative development route:
ADB forwarding to the Android Auto developer headunit server avoids replacing
the MTP function driver, but requires USB debugging authorization and starting
the server on the phone; that transport is not implemented here either.

## Protocol-library port investigation

Update 2026-09-18: the native port is implemented in `Aasdk.vcxproj` and linked
through `AndroidAutoSession`. Both protocol and decoder code build under MSVC.
Real hardware reaches Android Auto 1.7, successful TLS, and service discovery.
The current remaining blocker is that the phone stops responding after the
service discovery response. This is not the old Windows USB-open problem.
`SSL_ERROR_WANT_READ` after draining an in-memory TLS record is normal; the
upstream error-level log has been corrected. See `progress.md` for test results.

The following records the initial investigation, before the port:

OpenCarDev AASDK revision `9bf6adf933665dee26532201719fac14a047ccf1` was inspected
in `.tools/aasdk` (not linked or copied into our application). Its top-level
CMake unconditionally appends GCC flags (`-fPIC`, `-Wall`, `-pedantic`, `-g`,
`-O0`/`-O3`), while the protobuf build's non-macOS branch searches Linux paths.
It requires Boost, protobuf/Abseil, OpenSSL and libusb. A native port is necessary;
there is no finished AASDK/TLS/video integration hidden behind the probe button.
