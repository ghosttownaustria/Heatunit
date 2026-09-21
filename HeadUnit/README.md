# HeadUnit: Android Auto PoC (Windows und Linux)

C++20 / Qt 6, ein Quellbaum fuer Windows (Visual Studio 2026 x64) und Linux (CMake, siehe [Linux](docs/linux.md)).
Was sich pro System unterscheidet (USB-Suche, Treiber-Zugriff, Audioausgang), steckt hinter kleinen Schnittstellen,
siehe [Architektur](docs/architecture.md#platform-layer). Der Rest dieser Datei beschreibt die Bedienung und den
Windows-Build; unter Linux gilt dieselbe Bedienung. Die Windows-Schritte "Treiber reparieren" und die
Administrator-Abfrage entfallen dort, dafuer braucht Linux einmalig eine udev-Regel.

Status: USB discovery, AOA mode switching, Android
Auto version/TLS negotiation, service and H.264 channel handling, FFmpeg decoding
and Qt video rendering are implemented. Real phone testing (Samsung SM-F776B, on Windows) has
confirmed version 1.7, TLS, service discovery and projected video, including repeated
sessions and a clean stop. Touch (mouse), a simulated rotary knob with hard keys, and phone audio
over WASAPI with a simulated audio display work on hardware. There is no simulated phone data or AA screen.

**Bedienung: ein Knopf.** Handy per Datenkabel anschliessen, entsperren und
**Android Auto verbinden** anklicken. Die App erledigt alle Schritte selbst und meldet
jeden davon im Fenster: Handy suchen, bei Bedarf den USB-Treiber reparieren (einmal
die Windows-Abfrage nach Administratorrechten bestaetigen), Android Auto auf dem Handy
starten und das Video anzeigen. Auf dem Handy nur die Android-Auto-Hinweise bestaetigen.
Waehrend der Sitzung heisst derselbe Knopf **Verbindung beenden** (oder das Fenster
schliessen): Das Handy bekommt ein Goodbye, dann wird die USB-Schnittstelle freigegeben.

**Displaygroesse.** Solange keine Verbindung besteht, laesst sich neben dem Verbinden-Knopf die Displaygroesse
waehlen: 800 x 480, 1280 x 720 (HD), 1600 x 600 (Ultrawide) oder 1920 x 1080 (Full HD). Das Handy erfaehrt sie
beim Verbinden (Video-Aufloesung, Touchflaeche und passende Bilddichte), darum ist die Auswahl waehrend einer
Verbindung gesperrt. Die Wahl wird gemerkt (Windows-Benutzer, Registry `HKCU\Software\HeadUnit`); das leere
Bildfeld zeigt schon vor dem Verbinden die Form des gewaehlten Displays.
Android Auto kennt nur feste Video-Aufloesungen. Ein Display anderer Form, wie 1600 x 600, wird in das
naechstgroessere Bild eingepasst: Das Handy bekommt 1920 x 1080 mit 360 Pixel Rand (oben und unten je 180),
zeichnet seine Oberflaeche nur in den mittleren 1920 x 720 grossen Streifen (Bilddichte 240 dpi) und die App
zeigt nur diesen Streifen. Touch-Positionen zaehlen in Pixeln dieses Streifens. Weitere Groessen lassen sich
nach demselben Prinzip in `src/androidauto/DisplayConfig.h` ergaenzen.
Mit `HeadUnit.exe --display 1280x720` startet ein einzelner Lauf mit
einer Groesse, ohne sie zu merken (auch fuer die `--test-...`-Laeufe, die sonst immer 800 x 480 nehmen).

**Bedienen wie im Auto.** Neben dem Bild sitzt eine simulierte Mittelkonsole:
- **Touch:** Mit der Maus direkt auf dem Bild klicken und ziehen. Die Position wird auf das
  Touchdisplay des Handys umgerechnet (das Bild behaelt sein Seitenverhaeltnis, Klicks auf
  die schwarzen Raender zaehlen nicht).
- **Aufbau der Konsole:** Oben eine Reihe **MEDIA, TEL, NAV** und das Handy-Symbol (**CarPlay / Android
  Auto**), darunter **HOME** links und **BACK** rechts, dann der grosse runde Regler mit vier Pfeilen. Alle
  uebrigen Tasten stehen darunter im Abschnitt **Weitere Tasten**: MENU, OPTION, RADIO, MAP, Titel zurueck/vor
  und Play/Pause, Leiser, Stumm, Lauter, danach die Audio-Anzeige.
- **Runder Regler:** Mausrad auf dem Regler oder mit der Maus im Kreis ziehen dreht (ein Rastpunkt
  alle 15 Grad). Ein Klick ohne Ziehen auf einen der vier **Pfeile** am Rand ist die Pfeiltaste in diese
  Richtung, ein Klick auf den **Kreis in der Mitte** drueckt den Regler (Enter). Der Pfeil unter der Maus
  leuchtet auf. Ein Klick, der auf einem anderen Bereich endet als er begann, zaehlt nicht.
- **Tasten** (angelehnt an ein BMW-iDrive-Multimedia-Bedienteil): Media, Tel, Nav und Map rufen die
  entsprechende App auf dem Handy auf (Map und Nav oeffnen beide die Navigation), Back ist die
  Zurueck-Taste des Handys, Option sendet dessen Menue-Taste. Radio und Menu haben noch keine Belegung,
  weil es kein eigenes Betriebssystem gibt: sie schreiben nur eine Zeile ins Fenster-Log.
- **Home** hat zwei Stufen, solange ein Handy uebertragen wird: Beim ersten Druck wechselt das Handy auf
  seinen Startbildschirm (Karte, Medien, Telefon-Karten, z. B. Maps und Spotify). Der zweite Druck
  oeffnet das Home-Menue des Radios (nur ein Log-Eintrag), der dritte geht zurueck zum Handy-Startbildschirm.
  Ohne verbundenes Handy meldet Home nur das Radio-Menue. Ob das Handy gerade auf seinem Startbildschirm
  ist, liest die App am Symbol unten links im Bild ab.
- Das **Handy-Symbol** (CarPlay / Android Auto) startet die Verbindung, wenn noch keine besteht, sonst holt die
  Taste das Handy in den Vordergrund.
- **Audio-Anzeige:** Lautstaerke (30 Stufen), Stumm und je ein Pegel fuer Medien, Navigation und
  System. Die Anzeige zeigt, welche Tonspur des Handys gerade Audio liefert. Lautstaerke und Stumm
  wirken in der App (der Windows-Regler bleibt unberuehrt); mehr Lautstaerke schaltet Stumm aus.
- **Tastatur:** Pfeile, Enter (Regler druecken), Esc/Rueck (Back), Pos1 (Home), F1 Menu, F2 Option,
  F3 Media, F4 Radio, F5 Tel, F6 Nav, F7 Map, F8 CarPlay / Android Auto, Leertaste (Play/Pause),
  Bild hoch/runter (Titel), +/- (Lautstaerke), M (Stumm).
Der Ton laeuft ueber das Standard-Ausgabegeraet von Windows (WASAPI, andere Programme behalten
ihren Ton), unter Linux ueber PulseAudio/PipeWire oder ALSA. Das Mikrofon (Sprachbefehle, Telefonate) ist noch
nicht angebunden.

**Wenn etwas klemmt, versucht die App es selbst:**
- Das Handy im Accessory-Modus bekommt Android Auto per AOA-Neustart neu gestartet,
  ohne Kabel-Neustecken (ca. 10 Sekunden).
- Windows weist dem Handy im Dateiuebertragungs-Modus oft wieder Samsungs Treiber zu
  (`LIBUSB_ERROR_NOT_FOUND`). Die App stellt WinUSB dann selbst wieder her.
- Das Accessory-Geraet des Handys wird an der Seriennummer erkannt, nicht am USB-Port:
  Im Accessory-Modus meldet sich das Handy oft auf einem anderen Port (USB 2.0 statt
  SuperSpeed).
- Wechselt das Handy nicht in den Android-Auto-Modus (gesperrt, Hinweis am Handy nicht
  bestaetigt), fragt die App bis zu zweimal neu an und sagt, was am Handy zu tun ist.
- Antwortet Android Auto auf dem Handy nach ca. 20 Sekunden nicht, oder reagiert die USB-Verbindung des
  Handys gar nicht mehr (z. B. nach langer Pause im Accessory-Modus: Windows meldet "Ein an das System
  angeschlossenes Geraet funktioniert nicht"), startet die App die
  USB-Verbindung des Handys neu (wie Kabel abziehen und anstecken), prueft den Treiber
  und versucht es erneut (bis zu zweimal). Dafuer kommt die Windows-Abfrage.
- Klappt auch das nicht, steht im Fenster, was zu tun ist (meist: Handy entsperren
  oder das Kabel einmal abziehen und wieder anstecken).

**Stabilitaet:** Eine Sitzung endet mit einer konkreten Meldung, wenn das Kabel
gezogen wird, das Handy 30 Sekunden lang nichts mehr sendet oder in der Startphase
kein Fortschritt erkennbar ist (die Meldung nennt die haengende Stufe: Versionsabfrage,
TLS, Servicesuche oder Video). Nicht decodierbare Videopakete werden verworfen.


Beim ersten Build auf einem neuen Rechner einmal
`powershell -ExecutionPolicy Bypass -File HeadUnit/scripts/Prepare-Dependencies.ps1`
aus dem Repository-Hauptordner ausfuehren. Danach normal in Visual Studio bauen.
Der erste Abhaengigkeitsbuild kann laenger dauern. Die App und AASDK verwenden
native VS-Projekte; Protobuf-Dateien und DLL-Kopien werden automatisch erzeugt.

**Diagnose per Kommandozeile** (ohne Fenster, jeweils mit `HeadUnit.exe`): `--scan`
(USB-Geraete auflisten), `--probe-usb` (USB-Zugriff und AOA-Version pruefen),
`--start-accessory` (Accessory-Modus starten und Bulk-Paar pruefen), `--repair-driver`
(WinUSB wiederherstellen), `--recover-phone` (USB-Verbindung des Handys neu starten und
Treiber reparieren), `--test-projection` (kompletter Ablauf, erfolgreich nach zehn
angezeigten Videobildern), `--test-input` (schickt Regler-, Tasten- und Touch-Eingaben ans Handy), `--test-audio` (startet Wiedergabe leise und prueft, dass Ton am Ausgabegeraet ankommt), `--test-tone` (spielt zwei Sekunden leisen Ton ueber den Audioausgang, ohne Handy), `--test-console` (Media, Home, Home, Media, Home, Radio, Nav: prueft die Zwei-Stufen-Logik von Home am echten Handybild), `--test-keys` (Diagnose: spielt die Schritte aus `HEADUNIT_TEST_KEYS` ab (Zahl = Tastencode, `t:X:Y` = Tipp aufs Display, `c:name` = Konsolentaste), durch Komma getrennt, z. B. `3,t:42:438,c:home`, und speichert nach jedem Schritt ein Bild). Mit `HEADUNIT_TEST_SHOTS=<Ordner>` speichern die Tests Bilder des Handys und des Fensters. Details: [Windows-Verbindung](docs/windows_connection.md).

## Visual Studio 2026: oeffnen, bauen, starten

1. Im Repository-Hauptordner **HeadUnit.sln** mit Visual Studio 2026 oeffnen
   (Datei > Oeffnen > Projekt/Projektmappe). Nicht den Ordner als CMake-Projekt oeffnen.
2. Oben **Debug** und **x64** auswaehlen.
3. Falls erforderlich: Rechtsklick auf **HeadUnit** > **Als Startprojekt festlegen**.
4. **Strg+Umschalt+B** baut die Projektmappe. **F5** baut und startet mit Debugger;
   **Strg+F5** startet ohne Debugger.

Keine manuellen CMake-Befehle und keine Qt-VS-Erweiterung erforderlich. Die nativen
`.vcxproj`-Projekte verwenden das Toolset **v145**. Auf diesem Rechner ist
Visual Studio 2026 Insiders mit den C++-Buildtools installiert. Auf anderen
Rechnern wird die Workload **Desktopentwicklung mit C++** inklusive Windows SDK
benoetigt.

Das bereits vorhandene Qt-SDK unter `../.tools/Qt/6.8.3/msvc2022_64` wird
automatisch gefunden. Bei einem anderen SDK-Pfad `QtRoot` in `msbuild/Qt.props`
anpassen oder `QT_ROOT` vor dem Start von Visual Studio setzen. Das SDK muss ein
Qt-6-MSVC-x64-Kit sein. Die benoetigten Qt-DLLs und das Windows-Plattformplugin
werden beim Build automatisch neben die EXE kopiert.

Ausgabe: `out/vs2026/x64/Debug/HeadUnit.exe` bzw.
`out/vs2026/x64/Release/HeadUnit.exe`. Das Debugger-Arbeitsverzeichnis ist der
jeweilige Ausgabeordner; dort liegt auch `headunit.log`.

**CoreTests** ist ein separates Konsolenprojekt ohne Qt-Abhaengigkeit.
Zum Ausfuehren voruebergehend als Startprojekt festlegen und Strg+F5 druecken.
Danach wieder HeadUnit als Startprojekt setzen. Die Tests werden beim normalen
Solution-Build gebaut, aber nicht automatisch ausgefuehrt.

**ProtocolTests** prueft TLS 1.2/1.3 in beide Richtungen mit unterschiedlichen
Paketgroessen und den Sitzungsabbruch. `HeadUnit.exe --test-projection` startet
einen echten Verbindungstest und ist erst erfolgreich, wenn zehn decodierte
Handy-Frames im Qt-Fenster angezeigt wurden (90 Sekunden bis zum ersten Bild).
`HEADUNIT_PROTOCOL_TRACE=1` schaltet detaillierte AASDK-Ausgaben ein.

Die eingebundene AASDK und die davon abgeleitete Sitzungslogik stehen unter
GPL-3.0-or-later; siehe [Herkunft und Anpassungen](third_party/aasdk/PATCHES.md).

Die manuelle Qt-Runtime-Kopie deckt die aktuelle Widgets-Anwendung ab. Bei neuen
Qt-Modulen/Plugins `msbuild/Qt.props` und `msbuild/Qt.targets` erweitern. Fuer
Weitergabe an einen anderen PC den passenden VC++ Redistributable mit einplanen;
Debug ist fuer die lokale Entwicklung gedacht.

## Linux

Kurzfassung (Debian, Ubuntu, Raspberry Pi OS; Pakete, Erste Inbetriebnahme und Fehlersuche stehen in
[docs/linux.md](docs/linux.md)):

```sh
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev libboost-dev libssl-dev \
    libprotobuf-dev protobuf-compiler libusb-1.0-0-dev libavcodec-dev libavutil-dev libswscale-dev
cd HeadUnit
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
bash scripts/install-udev-rules.sh     # einmalig: Zugriff aufs Handy ohne root, danach Handy neu stecken
./out/build/linux-debug/HeadUnit
```

Die Linux-Teile (USB-Suche ueber libusb, Audio ueber miniaudio, Zugriffspruefung statt Treiber-Reparatur) sind auf
einem Windows-Rechner fuer Linux (x86-64, teils ARM64/ARM32) uebersetzt und ohne neue Warnungen geprueft, aber noch
nicht auf einem Linux-Rechner gebaut oder mit einem Handy ausgefuehrt worden: siehe "Stand der Pruefung" in
docs/linux.md.

## CMake (Windows und Linux)

CMake baut dieselben Quellen wie die Visual-Studio-Projekte: unter Linux ist es der einzige Weg, unter Windows
eine Alternative zu `HeadUnit.sln` (und der Weg fuer Visual Studio 2022).

Install Visual Studio 2022 with Desktop development with C++, Windows SDK and
CMake tools. Use the **Developer PowerShell for VS 2022**. Qt must be the x64
MSVC 2022 kit (tested with 6.8.3), not MinGW.

From `HeadUnit/`:

```powershell
$env:QT_ROOT = 'C:\Qt\6.8.3\msvc2022_64'
cmake --preset windows-vs2022
cmake --build --preset windows-debug
ctest --preset windows-debug
$env:PATH = "$env:QT_ROOT\bin;$env:PATH"
.\out\build\windows-vs2022\Debug\HeadUnit.exe
```

On the development machine used for this milestone the Qt base SDK is installed
locally at `../.tools/Qt/6.8.3/msvc2022_64`. For that kit, use:

```powershell
$env:QT_ROOT = (Resolve-Path '..\.tools\Qt\6.8.3\msvc2022_64').Path
```

Alternatively open this folder in VS 2022 with `QT_ROOT` set in its environment
and select the `windows-vs2022` configure preset.

```powershell
# Console scan, no GUI initialization. Qt DLLs still need to be on PATH.
.\out\build\windows-vs2022\Debug\HeadUnit.exe --scan
# Real Qt window plus real USB scan; exits when the result is displayed.
.\out\build\windows-vs2022\Debug\HeadUnit.exe --smoke-test
# Deploy app and Qt runtime to out/install/bin:
cmake --install out/build/windows-vs2022 --config Debug
```

For a standalone Release build, build/install with `--config Release` instead.
`headunit.log` is appended in the current working directory. The console mirrors
it. Log timestamps are UTC with milliseconds. Logs contain device serial numbers.
Exit codes: 0 = completed scan/UI run, 1 = startup/argument failure, 2 = scan errors.
Per-device descriptor warnings can coexist with a successful scan; inspect the log.
A zero exit code never means that Android Auto is supported or connected.
`--probe-usb` performs the real USB access/AOA query for exactly one detected
Android candidate; exit 3 means a probe failure or ambiguous selection.

## Phone check

1. Connect an unlocked Android phone with a data-capable USB cable.
2. Run `HeadUnit.exe --scan` (or the log of any run) to inspect VID/PID, strings,
   configurations, interfaces and endpoints of every USB device.
3. Record the evidence and phone/Android/AA versions in `docs/progress.md`.

ADB debugging is not required. If already enabled, its interface provides an
additional identification hint. Manufacturer IDs alone are only candidates:
Samsung also makes non-phone USB devices. Unknown/charge-only phones may not be
identified. Missing descriptors are shown as warnings, never invented.
The window itself has one button (**Android Auto verbinden**) and does the mode switch,
driver repair and session by itself; the individual steps stay available on the command line.

See [research and protocol plan](docs/android_auto.md), [architecture](docs/architecture.md),
[USB diagnostics](docs/usb.md), [dependencies](docs/dependencies.md), and [verified progress](docs/progress.md).
