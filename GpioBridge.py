#!/usr/bin/env python3
"""GpioBridge: shows the head unit's real buttons and rotary encoder and forwards them to HeadUnit.

The buttons and the encoder hang on the GPIO pins of the Raspberry Pi (each button between its pin and GND). The window
shows what is pressed and whether HeadUnit is connected. Every press is sent to HeadUnit over its remote API
(HeadUnit/docs/api.md: JSON lines over TCP on 127.0.0.1). Without HeadUnit, or without GPIO (e.g. on a PC), the window
still works: the buttons on the screen can be clicked to test.

Usage: python3 GpioBridge.py [--host 127.0.0.1] [--port 47050] [--no-gpio] [--invert-rotation]
Needs Python 3 with tkinter; on the Pi also gpiozero (Raspberry Pi OS: sudo apt install python3-tk python3-gpiozero).
"""

import argparse
import json
import os
import queue
import socket
import threading
import tkinter as tk
from dataclasses import dataclass
from typing import Callable, Dict, List, Optional

DEFAULT_PORT = 47050
RECONNECT_SECONDS = 2.0
STATUS_INTERVAL_SECONDS = 2.0
SOCKET_TIMEOUT_SECONDS = 2.0
BOUNCE_SECONDS = 0.02
ENCODER_BOUNCE_SECONDS = 0.002
PRESSED_COLOR = "#3cb043"
RELEASED_COLOR = "#d9d9d9"
CONNECTED_COLOR = "#3cb043"
DISCONNECTED_COLOR = "#c0392b"


@dataclass(frozen=True)
class Control:
    """One physical button: its label, the GPIO (BCM number) it hangs on and the API command it sends."""
    name: str
    label: str
    gpio: int
    command: dict


# The buttons, each between its pin and GND (GPIO numbers are BCM numbers, the pin numbers are for the wiring).
BUTTONS: List[Control] = [
    Control("up", "Rad UP\npin 19 / GPIO10", 10, {"cmd": "knob", "action": "up"}),
    Control("down", "Rad DOWN\npin 21 / GPIO9", 9, {"cmd": "knob", "action": "down"}),
    Control("left", "Rad LEFT\npin 23 / GPIO11", 11, {"cmd": "knob", "action": "left"}),
    Control("right", "Rad RIGHT\npin 27 / GPIO0", 0, {"cmd": "knob", "action": "right"}),
    Control("media", "MEDIA\npin 29 / GPIO5", 5, {"cmd": "console", "key": "media"}),
    Control("tel", "TEL\npin 31 / GPIO6", 6, {"cmd": "console", "key": "tel"}),
    Control("nav", "NAV\npin 33 / GPIO13", 13, {"cmd": "console", "key": "nav"}),
    Control("projection", "Android Auto\npin 35 / GPIO19", 19, {"cmd": "console", "key": "projection"}),
    Control("home", "HOME\npin 37 / GPIO26", 26, {"cmd": "console", "key": "home"}),
    Control("back", "BACK\npin 8 / GPIO14", 14, {"cmd": "console", "key": "back"}),
]
ENCODER_PUSH = Control("push", "Dreh-Knopf (SW)\npin 15 / GPIO22", 22, {"cmd": "knob", "action": "press"})
ENCODER_CLK_GPIO = 17   # pin 11
ENCODER_DT_GPIO = 27    # pin 13


class HeadUnitLink(threading.Thread):
    """Keeps the connection to HeadUnit and sends the commands that are queued.

    While HeadUnit is not connected the commands are dropped: a key that is pressed minutes later would surprise the
    driver. Events for the window (connection, log lines) are put into `events`.
    """

    def __init__(self, host: str, port: int, events: "queue.Queue[tuple]") -> None:
        super().__init__(daemon=True)
        self._host = host
        self._port = port
        self._events = events
        self._commands: "queue.Queue[dict]" = queue.Queue()
        self._isConnected = False
        self._isStopRequested = threading.Event()

    def Send(self, command: dict) -> None:
        """Queues a command; it is sent at once if HeadUnit is connected, otherwise dropped."""
        self._commands.put(command)

    def Stop(self) -> None:
        self._isStopRequested.set()

    def run(self) -> None:
        while not self._isStopRequested.is_set():
            connection = self._Connect()
            if connection is None:
                self._DropCommands(RECONNECT_SECONDS)
                continue
            self._SetConnected(True, "")
            try:
                self._Serve(connection)
            except (OSError, ValueError) as error:
                self._events.put(("log", "Connection lost: %s" % error))
            finally:
                connection.close()
                self._SetConnected(False, "")

    def _Connect(self) -> Optional[socket.socket]:
        try:
            return socket.create_connection((self._host, self._port), timeout=SOCKET_TIMEOUT_SECONDS)
        except OSError:
            return None

    def _DropCommands(self, seconds: float) -> None:
        """Waits `seconds` and drops what was pressed meanwhile."""
        try:
            self._commands.get(timeout=seconds)
            self._events.put(("log", "HeadUnit is not connected: key dropped"))
        except queue.Empty:
            pass

    def _Serve(self, connection: socket.socket) -> None:
        """Sends commands and a status query now and then until the connection breaks."""
        connection.settimeout(SOCKET_TIMEOUT_SECONDS)
        reader = connection.makefile("r", encoding="utf-8", newline="\n")
        while not self._isStopRequested.is_set():
            try:
                command = self._commands.get(timeout=STATUS_INTERVAL_SECONDS)
            except queue.Empty:
                command = {"cmd": "status"}
            connection.sendall((json.dumps(command) + "\n").encode("utf-8"))
            line = reader.readline()
            if not line:
                raise OSError("closed by HeadUnit")
            self._HandleReply(command, json.loads(line))

    def _HandleReply(self, command: dict, reply: dict) -> None:
        if command["cmd"] == "status":
            if reply.get("ok"):
                info = "volume %s%s, page %s, phone %s" % (reply.get("volume"), " (muted)" if reply.get("muted") else "",
                                                           reply.get("page"), "projected" if reply.get("projecting") else "-")
                self._events.put(("connection", True, info))
            return
        if not reply.get("ok"):
            self._events.put(("log", "HeadUnit refused %s: %s" % (json.dumps(command), reply.get("error"))))

    def _SetConnected(self, isConnected: bool, info: str) -> None:
        self._events.put(("connection", isConnected, info))


class Bridge:
    """Turns presses, from the GPIO pins or from the window, into commands for HeadUnit and events for the window."""

    def __init__(self, link: HeadUnitLink, events: "queue.Queue[tuple]") -> None:
        self._link = link
        self._events = events
        self._steps = 0

    def SetPressed(self, control: Control, isPressed: bool) -> None:
        """A button goes down or up; only going down is a command (the API taps the key)."""
        self._events.put(("pressed", control.name, isPressed))
        if isPressed:
            self._link.Send(control.command)

    def Rotate(self, detents: int) -> None:
        """The encoder turned by `detents` (positive: clockwise)."""
        self._steps += detents
        self._events.put(("rotation", self._steps, detents))
        self._link.Send({"cmd": "rotate", "detents": detents})


class GpioInputs:
    """The real buttons and the encoder, read with gpiozero. Unavailable without gpiozero (e.g. on a PC)."""

    def __init__(self, bridge: Bridge, isRotationInverted: bool) -> None:
        from gpiozero import DigitalInputDevice  # imported here: only the Pi has it
        self._devices: List[object] = []
        self._bridge = bridge
        self._rotationSign = -1 if isRotationInverted else 1
        for control in BUTTONS + [ENCODER_PUSH]:
            self._AddButton(DigitalInputDevice, control)
        self._clk = DigitalInputDevice(ENCODER_CLK_GPIO, pull_up=True, bounce_time=ENCODER_BOUNCE_SECONDS)
        self._dt = DigitalInputDevice(ENCODER_DT_GPIO, pull_up=True, bounce_time=ENCODER_BOUNCE_SECONDS)
        # One detent is one falling edge of CLK; DT tells the direction.
        self._clk.when_activated = self._OnClockEdge
        self._devices += [self._clk, self._dt]

    def _AddButton(self, deviceType: Callable, control: Control) -> None:
        # With the pull-up a pressed button pulls the pin to GND, which gpiozero reports as active.
        button = deviceType(control.gpio, pull_up=True, bounce_time=BOUNCE_SECONDS)
        button.when_activated = lambda: self._bridge.SetPressed(control, True)
        button.when_deactivated = lambda: self._bridge.SetPressed(control, False)
        self._devices.append(button)

    def _OnClockEdge(self) -> None:
        self._bridge.Rotate(self._rotationSign * (1 if self._dt.is_active else -1))

    def Close(self) -> None:
        for device in self._devices:
            device.close()


class BridgeWindow:
    """The window: one field per button that lights up while it is pressed, the encoder, and the connection."""

    def __init__(self, root: tk.Tk, bridge: Bridge, events: "queue.Queue[tuple]", gpioText: str) -> None:
        self._root = root
        self._bridge = bridge
        self._events = events
        self._fields: Dict[str, tk.Label] = {}
        root.title("HeadUnit GPIO bridge")
        self._BuildConnectionBar(gpioText)
        self._BuildWheel()
        self._BuildKeys()
        self._BuildEncoder()
        self._log = tk.Label(root, anchor="w", justify="left", text="")
        self._log.pack(fill="x", padx=8, pady=(4, 8))
        root.after(50, self._Poll)

    def _BuildConnectionBar(self, gpioText: str) -> None:
        bar = tk.Frame(self._root)
        bar.pack(fill="x", padx=8, pady=8)
        self._connection = tk.Label(bar, text=" Nicht verbunden ", fg="white", bg=DISCONNECTED_COLOR, font=("TkDefaultFont", 11, "bold"))
        self._connection.pack(side="left")
        self._info = tk.Label(bar, text="", anchor="w")
        self._info.pack(side="left", padx=8)
        tk.Label(bar, text=gpioText, anchor="e").pack(side="right")

    def _BuildWheel(self) -> None:
        frame = tk.LabelFrame(self._root, text="Rad")
        frame.pack(fill="x", padx=8, pady=4)
        positions = {"up": (0, 1), "left": (1, 0), "right": (1, 2), "down": (2, 1)}
        for control in BUTTONS[:4]:
            row, column = positions[control.name]
            self._AddField(frame, control, row, column)
        for column in range(3):
            frame.columnconfigure(column, weight=1)

    def _BuildKeys(self) -> None:
        frame = tk.LabelFrame(self._root, text="Tasten")
        frame.pack(fill="x", padx=8, pady=4)
        for index, control in enumerate(BUTTONS[4:]):
            self._AddField(frame, control, index // 3, index % 3)
        for column in range(3):
            frame.columnconfigure(column, weight=1)

    def _BuildEncoder(self) -> None:
        frame = tk.LabelFrame(self._root, text="Drehgeber")
        frame.pack(fill="x", padx=8, pady=4)
        self._AddField(frame, ENCODER_PUSH, 0, 1)
        left = tk.Button(frame, text="Drehen links (test)", command=lambda: self._bridge.Rotate(-1))
        right = tk.Button(frame, text="Drehen rechts (test)", command=lambda: self._bridge.Rotate(1))
        left.grid(row=1, column=0, sticky="ew", padx=4, pady=4)
        right.grid(row=1, column=2, sticky="ew", padx=4, pady=4)
        self._rotation = tk.Label(frame, text="Position 0")
        self._rotation.grid(row=1, column=1)
        for column in range(3):
            frame.columnconfigure(column, weight=1)

    def _AddField(self, parent: tk.Widget, control: Control, row: int, column: int) -> None:
        """A field showing whether the button is pressed; the mouse presses it too, for testing without the hardware."""
        field = tk.Label(parent, text=control.label, bg=RELEASED_COLOR, relief="raised", width=20, height=3)
        field.grid(row=row, column=column, padx=4, pady=4, sticky="nsew")
        field.bind("<ButtonPress-1>", lambda _event: self._bridge.SetPressed(control, True))
        field.bind("<ButtonRelease-1>", lambda _event: self._bridge.SetPressed(control, False))
        self._fields[control.name] = field

    def _Poll(self) -> None:
        """Applies the events the GPIO and link threads queued (Tk may only be touched from its own thread)."""
        try:
            while True:
                self._Apply(self._events.get_nowait())
        except queue.Empty:
            pass
        self._root.after(50, self._Poll)

    def _Apply(self, event: tuple) -> None:
        kind = event[0]
        if kind == "pressed":
            self._fields[event[1]].config(bg=PRESSED_COLOR if event[2] else RELEASED_COLOR, relief="sunken" if event[2] else "raised")
        elif kind == "rotation":
            self._rotation.config(text="Position %d (%s)" % (event[1], "rechts" if event[2] > 0 else "links"))
        elif kind == "connection":
            isConnected = event[1]
            self._connection.config(text=" Verbunden " if isConnected else " Nicht verbunden ", bg=CONNECTED_COLOR if isConnected else DISCONNECTED_COLOR)
            self._info.config(text=event[2] if isConnected else "")
        elif kind == "log":
            self._log.config(text=event[1])


def ParseArguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Shows the head unit's buttons and forwards them to HeadUnit.")
    parser.add_argument("--host", default="127.0.0.1", help="where HeadUnit runs (default 127.0.0.1)")
    parser.add_argument("--port", type=int, default=int(os.environ.get("HEADUNIT_API_PORT", DEFAULT_PORT)), help="HeadUnit's API port (default 47050)")
    parser.add_argument("--no-gpio", action="store_true", help="do not read the GPIO pins, only the window's own buttons")
    parser.add_argument("--invert-rotation", action="store_true", help="swap the encoder's direction")
    return parser.parse_args()


def OpenGpio(bridge: Bridge, arguments: argparse.Namespace) -> "tuple[Optional[GpioInputs], str]":
    """The GPIO inputs and a line saying whether they run."""
    if arguments.no_gpio:
        return None, "GPIO: aus (--no-gpio)"
    try:
        return GpioInputs(bridge, arguments.invert_rotation), "GPIO: aktiv"
    except Exception as error:  # gpiozero missing, or not a Pi: the window's buttons still work
        return None, "GPIO: nicht verfuegbar (%s)" % error.__class__.__name__


def main() -> None:
    arguments = ParseArguments()
    events: "queue.Queue[tuple]" = queue.Queue()
    link = HeadUnitLink(arguments.host, arguments.port, events)
    bridge = Bridge(link, events)
    gpio, gpioText = OpenGpio(bridge, arguments)
    root = tk.Tk()
    BridgeWindow(root, bridge, events, gpioText)
    link.start()
    try:
        root.mainloop()
    finally:
        link.Stop()
        if gpio:
            gpio.Close()


if __name__ == "__main__":
    main()
