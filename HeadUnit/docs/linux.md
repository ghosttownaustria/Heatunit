# Linux

Dieselben Quellen wie unter Windows: kein zweites Projekt. Was sich pro Plattform unterscheidet, steckt hinter
kleinen Schnittstellen (USB-Suche, Treiber-Reparatur, Audioausgang) und wird in `CMakeLists.txt` ausgewaehlt.
Siehe [Architektur](architecture.md#platform-layer).

Kabellos (Bluetooth und eigenes WLAN, Raspberry Pi): [Kabelloses Android Auto](wireless.md).

Neuer Rechner ohne alles: [Schritt-fuer-Schritt-Anleitung](linux-setup.md); Bauen und Starten in einem Schritt:
`bash BuildAndRun.sh` im Wurzelordner des Repositorys (`--help` zeigt die Optionen).

Ziel: Debian 12 / Ubuntu 22.04 oder neuer und Raspberry Pi OS (Bookworm), auf x86-64, ARM64 und 32-Bit-ARM.
Voraussetzung: C++20-Compiler (GCC 11 oder neuer), CMake 3.24 oder neuer, Qt 6.4 oder neuer.

**Stand der Pruefung:** Der Linux-Code ist auf einem Windows-Rechner mit clang gegen dieselben Header uebersetzt
worden: alle Quellen fuer x86-64 ohne neue Warnungen, die plattformabhaengigen Teile auch fuer ARM64 und ARM32. Auf
Windows wurden dieselben Pfade ausgefuehrt, soweit sie nicht Linux-spezifisch sind (libusb-Suche, miniaudio-Ton,
CMake-Build). Gebaut, gelinkt und mit einem echten Handy ausprobiert wurde unter Linux noch nichts: bitte beim ersten
Lauf `ctest` und danach `HeadUnit --scan`, `--test-tone` und `--probe-usb` pruefen (Abschnitt "Erste
Inbetriebnahme") und Abweichungen melden.

## Pakete

Debian, Ubuntu, Raspberry Pi OS:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
    qt6-base-dev libboost-dev libssl-dev libprotobuf-dev protobuf-compiler \
    libusb-1.0-0-dev libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev
```

Zum Ausfuehren braucht Qt ein Plattform-Plugin fuer die Bildschirmausgabe: unter X11 `libxcb-cursor0` (Qt 6.5 oder
neuer), unter Wayland `qt6-wayland`. Ein Kiosk ohne Desktop startet mit `QT_QPA_PLATFORM=eglfs`.

Fedora: `dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel boost-devel openssl-devel
protobuf-devel protobuf-compiler libusb1-devel` und ein FFmpeg **mit H.264-Decoder** (`ffmpeg-free` hat keinen; aus
RPM Fusion `ffmpeg-devel` installieren).

Ein Soundsystem muss laufen (PulseAudio, PipeWire mit `pipewire-pulse`, oder nur ALSA): das Programm sucht es
selbst (siehe "Ton"). Zum Bauen ist dafuer kein Entwicklungspaket noetig.

## Bauen und testen

Aus `HeadUnit/`:

```sh
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

`linux-release` baut optimiert. Nur der Kern ohne Qt und ohne Bibliotheken (Tests der Ablauf- und Erkennungslogik):
`cmake --preset linux-core-only`, dann `cmake --build --preset linux-core-only` und `ctest --preset linux-core-only`.
Das Programm liegt danach in `out/build/linux-debug/HeadUnit`. `HEADUNIT_AUDIO_BACKEND` (`auto`, `miniaudio`) und
`HEADUNIT_INSTALL_UDEV_RULES` (fuer Paketbauer) sind CMake-Optionen.

## Zugriff aufs Handy: die udev-Regel (einmalig)

Unter Linux gehoert das USB-Geraet dem Systemverwalter, bis eine udev-Regel dem angemeldeten Benutzer Zugriff gibt.
Ohne sie findet HeadUnit das Handy (Name und Seriennummer liest es aus sysfs, das braucht keine Rechte), kann es aber
nicht oeffnen und meldet "Keine Berechtigung". Einmal pro Rechner:

```sh
bash scripts/install-udev-rules.sh
```

Das Skript fragt nach dem sudo-Passwort, kopiert `packaging/linux/70-headunit-android.rules` nach
`/etc/udev/rules.d` und laedt udev neu. Danach das Handy **einmal abziehen und wieder anstecken**. Die Regel gibt dem
Benutzer, der am Rechner angemeldet ist (`uaccess`), und der Gruppe `plugdev` Zugriff auf das rohe USB-Geraet von
Android-Handys: im Normalmodus nach Hersteller (Google, Samsung, Motorola, HTC, Huawei, Xiaomi, OnePlus, LG, Sony, Vivo)
oder wenn das Geraet eine MTP/PTP- oder ADB-Schnittstelle hat, und im Android-Auto-Modus (`18D1:2D00` bis `2D05`). Mit
`bash scripts/install-udev-rules.sh --uninstall` verschwindet sie wieder.

Laeuft HeadUnit als Dienst ohne angemeldeten Benutzer, muss dessen Konto in der Gruppe `plugdev` sein.

## Erste Inbetriebnahme

Am besten in dieser Reihenfolge, jeweils aus dem Ordner mit dem Programm:

| Befehl | Prueft | Erwartet |
| --- | --- | --- |
| `./HeadUnit --scan` | USB-Suche ohne Fenster | Das Handy steht in `headunit.log` (VID/PID, Seriennummer, Schnittstellen), Exit 0 |
| `./HeadUnit --test-tone` | Audioausgang ohne Handy | Zwei Sekunden leiser Ton, Exit 0 |
| `./HeadUnit --probe-usb` | Zugriff und AOA-Version | "AOA version 2", Exit 0 (sonst Hinweis auf die udev-Regel) |
| `./HeadUnit` | Alles | Fenster, **Android Auto verbinden** |

Der Ablauf im Fenster ist derselbe wie unter Windows (siehe [README](../README.md)); die Windows-Schritte "Treiber
reparieren" und die Administrator-Abfrage entfallen. Weitere Testlaeufe (`--test-projection`, `--test-input`,
`--test-audio`, `--test-console`, `--test-keys`) sind plattformunabhaengig.

## Ton

Der Ton laeuft ueber miniaudio. Es sucht das Soundsystem beim Start selbst, in dieser Reihenfolge: PulseAudio (auch
das `pipewire-pulse` von PipeWire), ALSA, JACK. Jeder Tonstrom des Handys (Medien, Navigation, System) bekommt eine
eigene Verbindung, die das Soundsystem mit dem Ton anderer Programme mischt; im Lautstaerkemixer erscheint "HeadUnit".
`HEADUNIT_AUDIO_DEVICE=<Teil des Namens>` waehlt ein anderes Ausgabegeraet als den Standard. In `headunit.log` steht
pro Strom, ueber welches Soundsystem und welches Geraet er laeuft (`[AUDIO] ... output opened ... via PulseAudio on
'...'`). Das Mikrofon ist wie unter Windows noch nicht angebunden.

## Was anders ist als unter Windows

- **Kein Treiber zu reparieren.** Unter Windows muss das Handy an WinUSB gebunden sein, und die App stellt das selbst
  wieder her. Linux braucht nur die udev-Regel (oben). `HeadUnit --repair-driver` prueft deshalb nur, ob sich das Handy
  oeffnen laesst, und sagt sonst, was fehlt.
- **USB-Neustart bei einem Handy, das nicht antwortet** (`--recover-phone`, die App macht es selbst) ist ein
  Versuch, keine Zusage: erst ein USB-Reset ueber libusb, danach (nur mit Schreibrecht auf sysfs, also als root) das
  Geraet fuer 3 bis 8 Sekunden abmelden und wieder anmelden. Ob ein Handy dabei den Accessory-Modus verlaesst, hat sich
  auf dem Windows-Testgeraet je nach Verfahren unterschieden; hilft es nicht, sagt die App, dass das Kabel einmal
  abgezogen werden soll.
- **Dateimanager und MTP:** Zeigt der Dateimanager das Handy (GVFS/MTP), stoert das nicht: die Umschaltung in den
  Android-Auto-Modus braucht keine Schnittstelle des Handys. Der Accessory-Modus bekommt eine eigene Schnittstelle, die
  ein Kernel-Treiber notfalls abgegeben muss (libusb macht das automatisch).
- **Dateien:** Log `headunit.log` im Arbeitsverzeichnis; ist es nicht beschreibbar (Start aus einem Starter oder
  Dienst), in `$XDG_STATE_HOME/headunit` bzw. `~/.local/state/headunit`. Die Wahl der Displaygroesse steht in
  `~/.config/HeadUnit/HeadUnit.conf`.
- **Kein Installer** und kein Mitliefern von Bibliotheken: sie kommen aus den Systempaketen. `cmake --install` legt
  nur das Programm nach `bin`.

## Fehlersuche

| Meldung / Zeichen | Ursache | Abhilfe |
| --- | --- | --- |
| "Keine Berechtigung, das USB-Geraet des Handys zu oeffnen" (`LIBUSB_ERROR_ACCESS`) | udev-Regel fehlt oder Handy seit der Installation nicht neu gesteckt | `bash scripts/install-udev-rules.sh`, Handy neu stecken; `ls -l /dev/bus/usb/*/*` zeigt bei Erfolg eine ACL (`+`) am Geraet |
| Handy erscheint nicht in `--scan` | Kabel nur zum Laden, Handy gesperrt, oder USB-Modus "keine Datenuebertragung" | Datenkabel, Handy entsperren, in den Dateiuebertragungs-Modus stellen; `lsusb` und `dmesg` zeigen, ob der Kernel es sieht |
| "Mehrere Android-Geraete gefunden" | Zwei Handys oder ein Tablet am Bus | Nur ein Geraet anschliessen |
| `[AUDIO] ... sound system initialization failed` | Weder PulseAudio noch ALSA erreichbar | Soundsystem starten; unter PipeWire `pipewire-pulse` installieren; `HeadUnit --test-tone` zum Pruefen |
| Ton knackt | Das Protokoll liefert den Ton zu unregelmaessig | `[AUDIO] ... underruns` in `headunit.log` (pro Strom beim Beenden); Rechner nicht ausgelastet? |
| Qt: `could not connect to display` / `no Qt platform plugin` | Kein Bildschirm, oder Plattform-Plugin fehlt | `DISPLAY` bzw. `WAYLAND_DISPLAY` pruefen, `libxcb-cursor0` / `qt6-wayland` installieren, sonst `QT_QPA_PLATFORM` setzen; ohne Bildschirm testen mit `QT_QPA_PLATFORM=offscreen` |
| Kein Bild, aber Verbindung steht | FFmpeg ohne H.264-Decoder | `ffmpeg -decoders \| grep h264` |
