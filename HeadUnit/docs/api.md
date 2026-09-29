# Remote API

Other programs on the head unit (a steering wheel key service, scripts, `GpioBridge.py` for the real buttons) work
everything the simulated centre console offers over a local TCP connection. It works in every build, also in the car
window where the console is hidden.

- Address: `127.0.0.1`, port **47050**. `--api-port N` or `HEADUNIT_API_PORT=N` change it, `0` switches the API off.
  (5040 is not used: Windows keeps it.) If the port is taken the program logs a `[API]` warning and runs without the API.
- Only the loopback address listens; there is no authentication. Reaching it from outside needs a separate decision.
- One JSON object per line in (UTF-8, `\n`), one JSON object per line out. Objects are flat: text and whole numbers,
  no nesting; unknown extra fields are ignored. Several connections may be open; a bad line never closes one, a line
  longer than 4096 bytes does.
- The API is only active in the normal window mode and `--smoke-test`, not in the scripted phone tests.

## Commands

| Line | Effect |
| --- | --- |
| `{"cmd":"console","key":"home"}` | A controller key: `home`, `menu`, `option`, `media`, `radio`, `tel`, `nav`, `map`, `back`, `projection` |
| `{"cmd":"knob","action":"press"}` | The knob: `press` (push), `up`, `down`, `left`, `right` |
| `{"cmd":"rotate","detents":-2}` | Turn the knob by whole detents (-20 to 20, not 0; positive is clockwise) |
| `{"cmd":"key","name":"play_pause"}` | A media key: `previous`, `play_pause`, `next` |
| `{"cmd":"volume","delta":1}` | Volume steps (-30 to 30, not 0) |
| `{"cmd":"mute"}` | Toggle mute |
| `{"cmd":"status"}` | Ask for the state |

Every key is a tap (press and release); holding a key is not part of the API.

## Replies

- `{"ok":true}` for a command that ran.
- `{"ok":true,"volume":15,"muted":false,"page":"radio_home","projecting":false}` for `status`. `page` is one of
  `projection`, `projection_home`, `radio_home`, `multimedia`, `radio`, `settings`, `bluetooth`.
- `{"ok":false,"error":"..."}` for broken JSON, an unknown command or name, or a missing / invalid field; nothing
  happened then.

## Try it

```
python3 -c "import socket;s=socket.create_connection(('127.0.0.1',47050));s.sendall(b'{\"cmd\":\"status\"}\n');print(s.recv(200))"
```

## GpioBridge.py

`GpioBridge.py` (next to `BuildAndRun.sh`) is a small window for the real hardware on the Raspberry Pi: it shows which
buttons are pressed, whether HeadUnit is connected, and forwards presses and encoder turns through this API. It needs
Python 3 with tkinter and, on the Pi, gpiozero (`sudo apt install python3-tk python3-gpiozero`). Without HeadUnit or
without GPIO (a PC) it still runs: the fields of the window can be clicked to test. Presses are dropped while HeadUnit
is not connected. `--host`, `--port`, `--no-gpio`, `--invert-rotation` (swap the encoder direction).

| Control | Pin / GPIO (BCM) | Command |
| --- | --- | --- |
| Wheel UP / DOWN / LEFT / RIGHT | 19 / 10, 21 / 9, 23 / 11, 27 / 0 | `knob` up / down / left / right |
| MEDIA, TEL, NAV, Android Auto | 29 / 5, 31 / 6, 33 / 13, 35 / 19 | `console` media / tel / nav / projection |
| HOME, BACK | 37 / 26, 8 / 14 | `console` home / back |
| Encoder CLK, DT | 11 / 17, 13 / 27 | `rotate` +1 / -1 (one detent per CLK edge, DT gives the direction) |
| Encoder SW | 15 / 22 | `knob` press |
