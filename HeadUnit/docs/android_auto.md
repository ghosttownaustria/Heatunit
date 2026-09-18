# Android Auto investigation

Research date: 2026-09-16. Dates below are the latest default-branch commit dates
returned by the GitHub API during this investigation, not proof of protocol
compatibility. Revisions are recorded so the comparison can be repeated.

| Project | Last commit / revision prefix | Platforms and license evidence | Reuse decision |
| --- | --- | --- | --- |
| [f1xpl/aasdk](https://github.com/f1xpl/aasdk) | 2018-07-17 / `046b3b381595` | C++, libusb, SSL, protobuf; README declares GPLv3; API has no detected license | Historical protocol/reference baseline, not the default dependency |
| [f1xpl/openauto](https://github.com/f1xpl/openauto) | 2024-12-12 / `aa90412bf93b` | README lists Windows/Linux/Pi; GPLv3 declared; older Qt/application architecture | Reference for channel orchestration; do not copy the whole UI |
| [opencardev/aasdk](https://github.com/opencardev/aasdk) | 2026-06-11 / `9bf6adf93366` | Active C++ fork, GPLv3 in README; Linux packaging; Windows scripts are not evidence of native MSVC success | Preferred candidate for the next isolated protocol build experiment |
| [opencardev/openauto](https://github.com/opencardev/openauto) | 2026-02-08 / `4cc739b81362` | GPL-3.0, Windows/Linux stated; build documentation still refers to Qt5 | Study discovery/video service lifecycle; no direct Qt6 reuse assumed |
| [andreknieriem/open-headunit](https://github.com/andreknieriem/open-headunit) (formerly headunit-revived) | 2026-09-16 / `5ce51c5e60ed` | AGPL-3.0; Android application | Modern compatibility reference; not a Windows/Linux C++ dependency |
| [mossyhub/openautolink](https://github.com/mossyhub/openautolink) | 2026-09-10 / `f6cf3b29eef7` | GPL-3.0; Android/AAOS and Linux bridge, AASDK lineage | Inspect modern handshake/schema/codec changes if base AASDK fails |

Commit metadata source for each row: `https://api.github.com/repos/OWNER/REPO/commits?per_page=1`;
license/default branch: `https://api.github.com/repos/OWNER/REPO`.
License declarations must be checked against actual adopted files before import;
no third-party protocol code was copied into this milestone.

## Compatibility findings

The old AASDK baseline does not establish compatibility with current Android/AA
versions. Recent activity in a fork also does not prove it. Open-headunit contains
recent fixes and reports limitations after AA updates; OpenAutoLink reports real
AAOS deployments and modern codec support. These are upstream reports, not tests
of our Windows app. None of the inspected sources supplies a verified matrix for
our MSVC 2022 + Qt6 + phone setup. Android version and Android Auto app version
must both be recorded during hardware testing.

The [OpenCarDev AASDK PowerShell script](https://github.com/opencardev/aasdk/blob/9bf6adf933665dee26532201719fac14a047ccf1/build.ps1)
includes WSL/Git Bash/container paths. A script named `build.ps1` is not sufficient
evidence of native Windows support. Its CMake also contains Windows conditionals;
the actual native port must be built and tested. OpenAuto explicitly advertises
Windows, but its [current build guide](https://github.com/opencardev/openauto/blob/4cc739b813622739b09352655581072fc4d39280/README.md)
does not establish a modern Qt6 build.

## Component choices

- Now: Qt6 Widgets/Concurrent and Windows SDK hub discovery. No dependency on a
  full headunit distribution. Native enumeration follows the documented hub API
  approach illustrated by [Microsoft USBView](https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview).
- Next: evaluate pinned OpenCarDev AASDK behind a protocol adapter. Reuse its
  framing, protobuf/channel definitions and TLS machinery where the port works.
  Avoid reimplementing that stack from scratch. Keep its USB integration behind
  our transport boundary; do not run two independent owners of the same interface.
- Transport candidate: [libusb on Windows](https://github.com/libusb/libusb/wiki/Windows)
  and Linux. Windows access typically requires WinUSB/libusbK on the target
  interface; discovery through a hub does not test that requirement.
- TLS/protobuf: use the versions supported by the selected/pinned AASDK build,
  with a compile and negotiation test before committing the dependency lock.
  OpenSSL and protobuf are not linked until then.
- Video: first negotiate H.264 explicitly, then inspect real negotiated metadata
  and payload framing. AASDK/OpenAuto are references; newer projects also support
  H.265/VP9, so never treat every media packet as H.264. FFmpeg libavcodec and
  libswscale are the preferred portable decoder candidates for the first real
  stream. Qt renders decoded frames; it does not implement AA negotiation.

## Next protocol milestone, after device verification

1. Select a specific phone/interface and record driver, VID/PID, Android and AA versions.
2. Build a pinned AASDK revision with MSVC x64; resolve dependencies and licenses
   in a separate target. Preserve the working discovery build throughout.
3. Open the transport with bounded transfers. Query AOA protocol support (request
   51), then send identifying strings (52) and start accessory mode (53) for the
   selected phone. Re-enumerate after disconnect/reconnect.
4. Identify the accessory bulk pair, never the ADB pair. Implement packet framing,
   version exchange, TLS handshake and authentication through the adapter.
5. Negotiate service discovery and video, handle channel open/setup/start/ACKs,
   focus and required control/sensor exchanges. Log each actual state transition.
6. Decode real packets, log first decoded frame and display it in the Qt window.
   Only this proves the requested projection milestone. Touch and audio follow.

[AOA documentation](https://source.android.com/docs/core/interaction/accessories/aoa)
describes USB accessory negotiation, **not** the complete Android Auto projection
protocol. AOA mode, a successful USB open or a TLS connection alone must never
be logged as a fully established AA/video session. No protocol connection is
implemented in the present first milestone.
