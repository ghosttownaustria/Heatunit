# Remote control API - design

## Goal

Other programs on the head unit (steering wheel key service, scripts, remote apps) can trigger everything the simulated
centre console (`CarPanel`) offers, also in kiosk builds where the panel is hidden. First version: enough for early tests.

## Scope

Functions to expose (all exist today in `CarPanel`):
- Console keys: home, menu, option, media, radio, tel, nav, map, back, projection.
- Rotary knob: rotate by N detents, press, four arrows.
- Media keys: previous, play_pause, next.
- Volume up/down and mute.
- Read-only `status` query.

Out of scope: pushed events, authentication, network access beyond localhost, held keys (a command is press + release).

## Architecture

- `RemoteCommand` (portable core, no Qt): parses one JSON line into a command, executes it through injected callbacks
  (struct `RemoteCommandDeps`, like `AutoConnectDeps`) and returns the reply line. Covered by CoreTests.
- `RemoteServer` (Qt, `QTcpServer`, thin): listens on `127.0.0.1` only, splits input into lines, calls
  `RemoteCommand`, writes the reply. Runs in the UI thread.
- `MainWindow` owns the server in all builds (kiosk included) and supplies callbacks that call the same methods the
  panel uses: `PressConsole`, `SendKey`, `Rotate`, `AudioState::ChangeVolume/ToggleMute`. `CarPanel` is unchanged.
- No `Q_OBJECT` (project rule: only `headunit_wireless` uses moc); use `QTcpServer` signals via lambdas.

## Protocol

One JSON object per line in, one JSON object per line out.

```
{"cmd":"console","key":"home"}
{"cmd":"key","name":"play_pause"}        previous | play_pause | next
{"cmd":"rotate","detents":-2}
{"cmd":"knob","action":"press"}          press | up | down | left | right
{"cmd":"volume","delta":1}
{"cmd":"mute"}
{"cmd":"status"}
```

Replies: `{"ok":true}`, `{"ok":true,"volume":N,"muted":false,"page":"...","projecting":false}` for status, or
`{"ok":false,"error":"..."}` for malformed JSON, unknown command, missing/invalid fields. A bad line never closes the
connection. Lines longer than 4 KiB close it.

## Configuration

Default port 47050 (5040 is taken by a Windows service). `--api-port N` and `HEADUNIT_API_PORT` override; `0` disables the API. Port in use: log a warning
(`[API]`), the program keeps running without the API.

## Testing

- CoreTests: every command, invalid JSON, unknown key names, missing fields, delta/detents range.
- Manual: the running app was driven over TCP (a Python client); no smoke-test hook.
- Build: new files added to `CMakeLists.txt`, `HeadUnit.vcxproj(.filters)`, `tests/CoreTests.vcxproj`.

## Docs

New `docs/api.md`; update `architecture.md` and dated entry in `progress.md` (verified vs. not verified).

## Open (deliberately deferred)

Event push (state changes), authentication for non-local access.
