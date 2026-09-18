# USB discovery and diagnostics

WindowsUsbBackend enumerates `GUID_DEVINTERFACE_USB_HUB` through SetupAPI, opens
each present hub and queries its ports. External and root hubs are included.
It reads `USB_NODE_CONNECTION_INFORMATION_EX`, device strings and every available
configuration descriptor through `IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION`.
The hub handle requests GENERIC_WRITE as required by this API approach; operations
are descriptor reads only, not device resets, configuration changes or writes.

The parser walks descriptors with length/bounds validation and retains interface
alternate settings, endpoint addresses, transfer types, maximum packet sizes and
intervals. Every property is logged. Missing strings or configurations produce
per-device warnings and preserve the rest of the scan. Hub/enumeration failures
produce scan errors including numeric Win32 code and system message. Partial scan
results remain visible. No USB function driver is replaced.

## Android evidence

| Evidence | Meaning | Does not prove |
| --- | --- | --- |
| Known vendor or Android in strings | Candidate only; heuristic can have false positives/negatives | Phone, Android version, AA support |
| Interface FF/42/01 | ADB descriptor found; Android device likely | Smartphone rather than tablet/TV, AA support |
| VID 18D1, PID 2D00/2D01/2D04/2D05 | Android accessory data mode | AA session or endpoint access |
| Active configuration, interface 0/alt 0 with bulk IN/OUT | Accessory endpoint descriptors available for a data-mode PID | Driver can open them or phone accepts AA |

AOA audio-only PIDs 2D02/2D03 are excluded from accessory data readiness. The ADB
interface must never supply the accessory bulk pair. Manufacturer identifiers
are not a complete Android device database. Standard MTP descriptors alone do
not identify Android. USB descriptors cannot reliably distinguish phone/tablet
or report the Android OS version. Current detection is evidence-based discovery,
not an active AOA probe.

Sources: [AOA 1.0](https://source.android.com/docs/core/interaction/accessories/aoa),
[AOA 2.0 PID table](https://source.android.com/docs/core/interaction/accessories/aoa2),
[USBView API reference example](https://github.com/microsoft/Windows-driver-samples/tree/main/usb/usbview).

## Manual tests

Run once without a phone, attach an unlocked phone with a data cable, then scan
again. Record new VID/PID, strings, interfaces, endpoints and the detection reason.
Disconnect and rescan: the entry must disappear. Automatic hotplug is not yet
implemented. For missing devices check the cable, phone USB mode and Device
Manager. For missing strings inspect the exact IOCTL failure; access restrictions
or disconnects can make results incomplete. A failed port is not evidence that
no phone exists.

`--scan` performs the same real enumeration as the UI. A scan may block inside a
synchronous Windows driver IOCTL; the UI stays responsive, but closing waits for
the worker. Timeout/cancel support is a follow-up before long-running sessions.
Descriptor claims are not sufficient to open a WinUSB transport. Check the bound
driver on the selected device and again after AOA re-enumeration. Do not replace
drivers on a whole composite device as a blanket troubleshooting step.
