# HeadUnit: Windows Android Auto PoC

C++20 / Visual Studio 2026 x64 / Qt 6. USB discovery, AOA mode switching, Android
Auto version/TLS negotiation, service and H.264 channel handling, FFmpeg decoding
and Qt video rendering are implemented. Real phone testing has confirmed version
1.7, TLS and service discovery; projected video is not yet hardware-verified.
There is no simulated phone data or AA screen.

**Verbinden:** **Android Auto verbinden** anklicken. Ist genau ein Android-Handy
angeschlossen, wird es automatisch verwendet; bei mehreren zuerst eines in der Liste
auswaehlen. Der Knopf uebernimmt auch den Accessory-Moduswechsel. Hinweise auf dem
entsperrten Handy bestaetigen.

**Beenden:** **Verbindung beenden** (oder das Fenster schliessen) schickt dem Handy
zuerst ein Android-Auto-Goodbye und wartet bis zu 2 Sekunden auf die Bestaetigung,
bevor die USB-Verbindung getrennt und die Schnittstelle freigegeben wird. Danach
startet die App nach kurzer Pause automatisch einen neuen Scan, sodass das Handy
sofort wieder verbunden werden kann. Beendet das Handy die Sitzung selbst, laeuft
derselbe Ablauf.

**Stabilitaet:** Eine Sitzung endet mit einer konkreten Meldung, wenn das Kabel
gezogen wird, das Handy 30 Sekunden lang nichts mehr sendet oder in der Startphase
90 Sekunden lang kein Fortschritt erkennbar ist (die Meldung nennt die haengende
Stufe: Versionsabfrage, TLS, Servicesuche oder Video). Einzelne nicht decodierbare
Videopakete werden verworfen statt die Sitzung zu beenden. Hilft nach einer
fehlgeschlagenen Sitzung ein erneutes Verbinden nicht, das Kabel neu einstecken.
Audio und Touch-Eingabe sind noch nicht implementiert.

Beim ersten Build auf einem neuen Rechner einmal
`powershell -ExecutionPolicy Bypass -File HeadUnit/scripts/Prepare-Dependencies.ps1`
aus dem Repository-Hauptordner ausfuehren. Danach normal in Visual Studio bauen.
Der erste Abhaengigkeitsbuild kann laenger dauern. Die App und AASDK verwenden
native VS-Projekte; Protobuf-Dateien und DLL-Kopien werden automatisch erzeugt.

**Neu: echter USB-Zugriffstest.** Nach dem Scan das Handy auswaehlen und
**Android Auto: USB-Zugriff pruefen** anklicken. Der Test versucht das Geraet mit
libusb zu oeffnen und dessen AOA-Version abzufragen. Beim angeschlossenen Samsung
funktioniert dies nach der freigegebenen Treiberumstellung: AOA-Version 2 wurde
am 2026-09-17 tatsaechlich empfangen. Details, Treiber-Rueckweg und der naechste
Schritt stehen in [Windows-Verbindung](docs/windows_connection.md). Dies ist noch
kein vollstaendiger Android-Auto-Verbindungsaufbau.

**Accessory-Modus:** Handy auswaehlen und **Android Auto: Accessory-Modus starten**
anklicken. Die App sendet die AOA-Kennung, startet den Moduswechsel und wartet bis
zu 15 Sekunden auf das Geraet am selben USB-Port. Danach prueft sie das Bulk-Paar.
Am echten Samsung erfolgreich getestet: `04E8:6860` wird zu `18D1:2D00`,
Interface 0 ist zugreifbar (IN `0x81`, OUT `0x01`). Die Geraeteliste wird nach dem
Versuch aktualisiert. Alternativ: `HeadUnit.exe --start-accessory`.
Die Pruefung gibt das Interface anschliessend frei; erst **Android Auto verbinden**
startet die Protokollsitzung. Abziehen und erneut anschliessen beendet normalerweise
den Accessory-Modus.

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

## Optional: existing CMake build

CMake remains available for portability and the older VS2022 build path.

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
2. Start the app or press **Scan USB devices** after connecting it.
3. Expand the device to inspect VID/PID, strings, configurations, interfaces and endpoints.
4. Record the evidence and phone/Android/AA versions in `docs/progress.md`.

ADB debugging is not required. If already enabled, its interface provides an
additional identification hint. Manufacturer IDs alone are only candidates:
Samsung also makes non-phone USB devices. Unknown/charge-only phones may not be
identified. Missing descriptors are shown as warnings, never invented.
This milestone performs no driver installation, mode switch or bulk transfer.

See [research and protocol plan](docs/android_auto.md), [architecture](docs/architecture.md),
[USB diagnostics](docs/usb.md), [dependencies](docs/dependencies.md), and [verified progress](docs/progress.md).
