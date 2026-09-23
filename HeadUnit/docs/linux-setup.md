# Linux: von einem frisch installierten System bis zum laufenden HeadUnit

Schritt fuer Schritt, auf einem Rechner, auf dem noch nichts eingerichtet ist. Alle Befehle im Terminal, jeweils
nacheinander. Hintergrund und Fehlersuche stehen in [Linux](linux.md).

**Geeignet:** Debian 12, Ubuntu 24.04 und Raspberry Pi OS (Bookworm): deren Paketversionen erfuellen CMake 3.24 und
Qt 6.4. Ubuntu 22.04 hat zu alte Versionen (CMake 3.22, Qt 6.2) und geht ohne Zusatzquellen nicht. Die Anleitung ist
noch nicht auf einem frischen System durchgespielt worden (siehe "Stand der Pruefung" in [Linux](linux.md)).

> **Achtung, Branch:** Die Linux-Unterstuetzung steht zur Zeit nur auf dem Branch `develope`, nicht auf `main`.
> Schritt 4 checkt ihn aus. Sobald `develope` nach `main` gemergt ist, entfaellt dieser Schritt.

## 1. System aktualisieren

```sh
sudo apt update
sudo apt upgrade -y
```

Danach ggf. neu starten (`sudo reboot`), wenn ein neuer Kernel installiert wurde.

## 2. Git installieren

```sh
sudo apt install -y git
```

## 3. Repository holen

```sh
cd ~
git clone https://github.com/ghosttownaustria/Heatunit.git
cd Heatunit
```

Ist das Repository privat, fragt Git nach dem Benutzernamen und einem **Personal Access Token** als Passwort (GitHub:
Settings, Developer settings, Personal access tokens). Alternativ mit SSH-Schluessel klonen.

## 4. Branch `develope` auswaehlen

```sh
git checkout develope
```

## 5. Abhaengigkeiten installieren

Entweder mit dem Skript (es prueft, welche Pakete fehlen, und installiert genau die; danach baut es gleich, also
schon Schritt 7 ohne Start):

```sh
bash BuildAndRun.sh --install-deps --no-run
```

oder von Hand:

```sh
sudo apt install -y build-essential cmake ninja-build pkg-config \
    qt6-base-dev libboost-dev libssl-dev libprotobuf-dev protobuf-compiler \
    libusb-1.0-0-dev libavcodec-dev libavutil-dev libswscale-dev \
    libxcb-cursor0
```

Wozu die Pakete gut sind:

| Paket | Wofuer |
| --- | --- |
| `build-essential`, `cmake`, `ninja-build`, `pkg-config` | Compiler (GCC 11 oder neuer) und Build-Werkzeuge |
| `qt6-base-dev` | Oberflaeche (Qt 6.4 oder neuer) |
| `libboost-dev`, `libssl-dev`, `libprotobuf-dev`, `protobuf-compiler` | Android-Auto-Protokoll (AASDK) |
| `libusb-1.0-0-dev` | Zugriff aufs Handy per USB |
| `libavcodec-dev`, `libavutil-dev`, `libswscale-dev` | H.264-Videodekodierung (FFmpeg) |
| `libxcb-cursor0` | Qt-Plattform-Plugin unter X11 (Qt 6.5 oder neuer; unter Wayland stattdessen `qt6-wayland`) |

Der Ton laeuft ueber PulseAudio, PipeWire (`pipewire-pulse`) oder ALSA. Auf einem normalen Desktop-System ist das schon
da. Auf einem Minimalsystem: `sudo apt install -y pulseaudio` (oder `pipewire-pulse`).

## 6. Zugriff aufs Handy erlauben (einmalig)

Ohne diese udev-Regel findet HeadUnit das Handy, darf es aber nicht oeffnen ("Keine Berechtigung"):

```sh
bash HeadUnit/scripts/install-udev-rules.sh
```

Das Skript fragt nach dem sudo-Passwort. Danach das Handy **einmal abziehen und wieder anstecken**. (Geht auch
zusammen mit dem Bauen: `bash BuildAndRun.sh --udev`.)

## 7. Bauen, testen, starten

```sh
bash BuildAndRun.sh
```

Das konfiguriert mit CMake (Preset `linux-debug`), baut mit Ninja, fuehrt `ctest` aus und startet `HeadUnit`.
Das fertige Programm liegt in `HeadUnit/out/build/linux-debug/HeadUnit`, das Log `headunit.log` daneben.

Haeufige Varianten:

```sh
bash BuildAndRun.sh release                  # optimiert bauen (linux-release)
bash BuildAndRun.sh --pull                   # vorher git pull
bash BuildAndRun.sh --no-run                 # nur bauen und testen
bash BuildAndRun.sh --clean                  # Build-Ordner neu anlegen
bash BuildAndRun.sh -j 2                     # nur 2 Compiler-Prozesse (Raspberry Pi mit wenig RAM)
bash BuildAndRun.sh release -- --display 1280x720   # Argumente an HeadUnit
bash BuildAndRun.sh --help
```

Beim ersten Bauen dauert es einige Minuten (AASDK und Protobuf sind gross). Auf einem Raspberry Pi mit 1 bis 2 GB RAM
`-j 2` verwenden, sonst geht dem Compiler der Speicher aus.

## 8. Erste Inbetriebnahme pruefen

Handy per Datenkabel anstecken, entsperren, Dateiuebertragung aktivieren. Dann nacheinander (`--no-test` spart das
erneute Testen; alles hinter `--` geht an HeadUnit):

```sh
bash BuildAndRun.sh --no-test -- --scan          # USB-Suche: Handy steht in headunit.log
bash BuildAndRun.sh --no-test -- --test-tone     # zwei Sekunden leiser Ton
bash BuildAndRun.sh --no-test -- --probe-usb     # Zugriff und "AOA version 2"
bash BuildAndRun.sh --no-test                    # das Programm mit Fenster
```

Im Fenster **Android Auto verbinden** waehlen. Was jeder Test erwartet, steht in
[Linux, Erste Inbetriebnahme](linux.md#erste-inbetriebnahme).

## 9. Ohne Desktop (Kiosk, SSH)

- Ohne laufenden Desktop, direkt auf dem Bildschirm: `QT_QPA_PLATFORM=eglfs bash BuildAndRun.sh`
- Ueber SSH gibt es keinen Bildschirm. Ohne Fenster testen: `QT_QPA_PLATFORM=offscreen bash BuildAndRun.sh --no-test -- --scan`

## 10. Spaeter aktualisieren

```sh
cd ~/Heatunit
bash BuildAndRun.sh --pull
```

`--reset` verwirft dabei lokale Aenderungen (`git reset --hard`) und holt danach den neuesten Stand.

## Kurzfassung

```sh
sudo apt update && sudo apt upgrade -y
sudo apt install -y git
cd ~ && git clone https://github.com/ghosttownaustria/Heatunit.git && cd Heatunit
git checkout develope
bash BuildAndRun.sh --install-deps --udev
```

## Wenn etwas nicht klappt

| Problem | Abhilfe |
| --- | --- |
| `bash: ... : bad interpreter` / `\r` in Fehlermeldungen | Skript hat Windows-Zeilenenden. Frisch geklont passiert das nicht (`.gitattributes` erzwingt LF); sonst `sed -i 's/\r$//' BuildAndRun.sh` |
| "CMake ... ist zu alt" | Debian 12 / Ubuntu 24.04 / Raspberry Pi OS Bookworm verwenden |
| `Could not find Qt6` | `qt6-base-dev` fehlt oder ist aelter als 6.4 (`apt policy qt6-base-dev`) |
| "Keine Berechtigung" beim Oeffnen des Handys | Schritt 6 wiederholen, Handy neu stecken |
| Kein Fenster, `could not connect to display` | Auf dem Rechner selbst starten (nicht per SSH) oder Schritt 9 |
| Kein Bild trotz Verbindung | FFmpeg ohne H.264-Decoder: `ffmpeg -decoders \| grep h264` |

Weitere Meldungen: [Linux, Fehlersuche](linux.md#fehlersuche).
