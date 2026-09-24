# Kabelloses Android Auto (Linux, Raspberry Pi)

Statt USB-Kabel: HeadUnit erzeugt selbst ein WLAN, schaltet Bluetooth ein und ist als **HEATUNIT** sichtbar, solange der
kabellose Modus laeuft. Das Handy wird einmal gekoppelt, danach verbindet es sich von selbst. Nur Linux; unter Windows
bleibt es beim Kabel.

**Stand der Pruefung:** Auf dem Raspberry Pi 4 mit einem echten Handy (Samsung SM-F776B): Kopplung klappt, das Handy
ist "fuer Anrufe und Audio verbunden" und zeigt "Wird mit Android Auto verbunden", tritt dem WLAN aber nicht bei. Zwei
wahrscheinliche Ursachen sind behoben, aber noch nicht am Handy bestaetigt: Das WLAN startete erst, als das Handy schon
auf Antwort wartete (jetzt laeuft es vorher), und ein schon verbundenes Handy suchte den Android-Auto-Dienst nicht erneut
(jetzt wird es neu verbunden). Ohne Handy getestet (`ctest`): Nachrichtenformat, der Bluetooth-Dialog gegen ein
simuliertes Handy, Socket-Transport.

## Wie es funktioniert

```text
Handy                                      HeadUnit (Raspberry Pi)
                                            WLAN-Hotspot HEATUNIT-AA starten (einige Sekunden)
                                            Bluetooth an (rfkill entsperren), sichtbar als HEATUNIT
  |  1. Bluetooth: koppeln (einmal)  --->   Kopplung ohne PIN, Handy wird "vertraut"
  |        <--- gekoppelte Handys verbinden (ein schon verbundenes Handy zuerst trennen)
  |  2. Bluetooth: Dienst "Android Auto Wireless" (RFCOMM)
  |        <--- sofort: "verbinde dich mit 10.42.0.1:5288"
  |        ---> "welches WLAN?"
  |        <--- SSID, Passwort, BSSID, WPA2
  |  3. WLAN: Handy tritt dem Hotspot HEATUNIT-AA bei
  |  4. TCP zu 10.42.0.1:5288  --->   dieselbe Android-Auto-Sitzung wie ueber USB
  |                                         kabelloser Modus vorbei: Hotspot wieder aus
```

- Der **Hotspot kommt vom HeadUnit-Rechner**, nicht vom Handy. Er wird mit NetworkManager (`nmcli`) angelegt, der auch
  die Adressen verteilt (`ipv4.method shared`). Er startet vor Bluetooth und laeuft, solange der kabellose Modus an ist:
  Das Handy erwartet die erste Nachricht, sobald es den Android-Auto-Dienst geoeffnet hat, und darf nicht erst auf
  `nmcli` warten (die kabellosen Adapter und openauto haben ihr WLAN ebenfalls immer schon an). Ein Hotspot, den ein
  abgebrochener Lauf hinterlassen hat, wird beim naechsten Start entfernt; Ctrl+C und `systemctl stop` beenden das
  Programm geordnet (Sitzung beenden, Hotspot abbauen).
- **Bluetooth** dient nur dazu, das Handy zu finden und ihm die WLAN-Daten zu geben. HeadUnit spricht dafuer mit
  BlueZ ueber D-Bus (QtDBus, steckt in `qt6-base-dev`) und schaltet es selbst ein: Adapter einschalten, und wenn er
  per rfkill gesperrt ist (Raspberry Pi OS macht das), zuerst entsperren (ueber `/dev/rfkill`, sonst `sudo -n rfkill`).
- **Schon verbundenes Handy:** PipeWire verbindet ein gekoppeltes Handy "fuer Anrufe und Audio" von selbst, auch wenn
  HeadUnit gar nicht im kabellosen Modus ist. Android Auto sucht seinen Dienst beim Verbinden; gibt es ihn da noch
  nicht, bleibt "Wird mit Android Auto verbunden" stehen. Darum trennt HeadUnit beim Start des kabellosen Modus jedes
  schon verbundene Handy und verbindet es neu (nur Geraete, die BlueZ als Handy fuehrt; Log `[BT]`: "reconnecting it").
- **Sicherheitsmodus:** Das Handy liest ihn in Androids eigener Zaehlung (WPA 4, WPA2 8, beides 12). Die importierte
  Aufzaehlung `WifiSecurityMode` zaehlt 0 bis 9; ihr `WPA2_PERSONAL` (5) kennt das Handy nicht und tritt dem WLAN dann
  nie bei ("Wird mit Android Auto verbunden" bleibt stehen). HeadUnit sendet deshalb 8 (`kWifiSecurityWpa2Personal`).
- Ab dem TCP-Anschluss laufen Protokoll, Video, Ton und Eingabe **unveraendert**: nur der Transport ist ein anderer
  (`SocketTransport` statt `ProjectionTransport`).

Code: `src/wireless/` (`WirelessProtocol` Nachrichten, `WirelessLink` der Bluetooth-Dialog bis zur TCP-Verbindung,
`BluetoothService` BlueZ, `Hotspot` nmcli, `SocketTransport` TCP, `WirelessConnect` der ganze Ablauf).

## Voraussetzungen

| | |
| --- | --- |
| Rechner | Raspberry Pi 4 (Bluetooth und WLAN eingebaut) mit Raspberry Pi OS Bookworm |
| Dienste | `NetworkManager` (Bookworm-Standard) und `bluetooth` (BlueZ); `systemctl status NetworkManager bluetooth` |
| WLAN-Land | muss gesetzt sein, sonst startet der 5-GHz-Hotspot nicht: `sudo raspi-config` → Localisation Options → WLAN Country |
| Bluetooth | schaltet HeadUnit selbst ein. Nur bei einer Sperre per Hardware (`rfkill list`: "Hard blocked: yes") geht das nicht |
| Handy | Android Auto **mit kabellos-Unterstuetzung** (Android 11 oder neuer; bei manchen Handys ist es in den Android-Auto-Einstellungen abgeschaltet), WLAN und Bluetooth an, 5 GHz empfohlen |
| Bauen | nichts Neues: `qt6-base-dev` bringt QtDBus mit. Ohne QtDBus baut CMake ohne kabellos und warnt (`-DHEADUNIT_WIRELESS=OFF` schaltet es bewusst ab) |

**Achtung SSH:** Der WLAN-Chip des Pi wird zum Hotspot und kann dann kein WLAN-Client mehr sein. Eine SSH-Verbindung
**ueber WLAN bricht ab**, sobald der Hotspot startet (also sobald der kabellose Modus startet). Zum Entwickeln ein Netzwerkkabel (Ethernet) verwenden. Nach dem
Beenden verbindet sich der Pi wieder mit seinem WLAN.

## Ausprobieren, Schritt fuer Schritt

Jeweils aus `HeadUnit/out/build/linux-release` (oder `-debug`), nach `bash BuildAndRun.sh --no-run`.

**1. Hotspot** (ohne Handy-Zusammenspiel):

```sh
./HeadUnit --test-hotspot
```

Gibt Netzname, Passwort und Adresse aus und haelt den Hotspot 60 Sekunden (`HEADUNIT_TEST_SECONDS=300`). Am Handy
manuell dem WLAN beitreten: klappt das, ist der Hotspot in Ordnung. Fehlermeldungen nennen die haeufigsten Ursachen.

**2. Bluetooth und Kopplung:**

```sh
./HeadUnit --test-bluetooth
```

Macht den Pi 120 Sekunden lang als **HEATUNIT** sichtbar. Am Handy: Einstellungen → Bluetooth → HEATUNIT koppeln.
Danach in Android Auto (Einstellungen → Verbundene Geraete → Bluetooth) das Auto verbinden. Besteht der Test, hat das
Handy den Dienst geoeffnet, die WLAN-Daten erfragt und bekommen. **`headunit.log` zeigt jeden Schritt** (`[BT]`:
Kopplung, `Connected = true`, "opened the Android Auto Wireless service"; `[WLAN]`: die Nachrichten des Handys). Steht
nach dem Koppeln kein "opened the Android Auto Wireless service" im Log, verbindet das Handy den Dienst nicht von
selbst (siehe "Offene Punkte").

**3. Alles zusammen:**

```sh
./HeadUnit --wireless
```

oder im Fenster auf **Android Auto kabellos** klicken. Ablauf: Hotspot starten, sichtbar werden, gekoppelte Handys
verbinden, auf das Handy warten, WLAN-Daten uebergeben, Handy tritt bei, Sitzung. Das Fenster zeigt jeden Schritt. Mit
**Verbindung beenden** endet alles und der Hotspot wird abgebaut. Aus BuildAndRun: `bash BuildAndRun.sh release -- --wireless`.

Die Kachel **Android Auto** und der Knopf **Android Auto verbinden** sind nur fuer das USB-Kabel. Ohne `--wireless` oder
**Android Auto kabellos** gibt es den Android-Auto-Dienst per Bluetooth nicht; das Handy verbindet sich trotzdem "fuer
Anrufe und Audio" (PipeWire) und bleibt dann bei "Wird mit Android Auto verbunden" stehen.

## Einstellungen (Umgebungsvariablen)

| Variable | Bedeutung | Standard |
| --- | --- | --- |
| `HEADUNIT_BT_NAME` | Bluetooth-Name | `HEATUNIT` |
| `HEADUNIT_WIFI_SSID` | Name des Hotspots | `HEATUNIT-AA` |
| `HEADUNIT_WIFI_PASSWORD` | Passwort des Hotspots (8 bis 63 Zeichen) | 16 zufaellige Zeichen, gespeichert in `~/.config/HeadUnit/HeadUnit.conf` |
| `HEADUNIT_WIFI_INTERFACE` | WLAN-Geraet | das erste, das NetworkManager kennt (`wlan0`) |
| `HEADUNIT_WIFI_BAND` | `a` = 5 GHz, `bg` = 2,4 GHz | `a` |
| `HEADUNIT_WIFI_CHANNEL` | Kanal | 36 (`a`) bzw. 6 (`bg`) |
| `HEADUNIT_TEST_SECONDS` | Dauer von `--test-bluetooth` / `--test-hotspot` | 120 / 60 |

Das Passwort muss der Nutzer nie eintippen: das Handy bekommt es ueber Bluetooth. Es bleibt gleich, damit das Handy
sein gemerktes WLAN weiter benutzen kann.

## Sicherheit

Solange HeadUnit im kabellosen Modus laeuft, ist der Pi per Bluetooth sichtbar und **koppelt ohne PIN** (wie ein
Auto). Das Handy bestaetigt die Kopplung selbst. Die Sichtbarkeit endet mit dem Beenden. Der Hotspot ist mit WPA2
geschuetzt; er hat keinen Internetzugang und ist nur fuer das Handy gedacht.

## Fehlersuche

| Meldung / Zeichen | Ursache | Abhilfe |
| --- | --- | --- |
| "NetworkManager (nmcli) ist nicht installiert" | Anderes Netzwerk-System | `sudo apt install network-manager` und als Netzwerkverwaltung aktivieren |
| "Der WLAN-Hotspot startet nicht" | WLAN-Land fehlt, Kanal nicht erlaubt, Chip kann den Kanal nicht als Access Point | Land setzen (siehe Voraussetzungen); 2,4 GHz versuchen: `HEADUNIT_WIFI_BAND=bg` |
| "Bluetooth ist gesperrt (rfkill) und liess sich nicht entsperren" | Kein Zugriff auf `/dev/rfkill` (Start ueber SSH) und kein `sudo` ohne Passwort | Einmalig `sudo rfkill unblock bluetooth`; oder am Bildschirm angemeldet starten |
| "Der Bluetooth-Adapter laesst sich nicht einschalten" | Adapter haengt (Raspberry Pi: UART-Bluetooth) | `bluetoothctl show`, `sudo systemctl restart bluetooth hciuart`, notfalls neu starten |
| "Der Bluetooth-Dienst (bluetoothd) laeuft nicht" | Dienst abgeschaltet | `sudo systemctl enable --now bluetooth` |
| "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden" | Keine Rechte am System-D-Bus | Nutzer in die Gruppe `bluetooth`: `sudo usermod -aG bluetooth $USER`, neu anmelden |
| HEATUNIT erscheint am Handy nicht | Nicht sichtbar | `bluetoothctl show` (Discoverable: yes), Log `[BT]` |
| Kopplung klappt, aber nichts passiert | Das Handy oeffnet den Dienst nicht von selbst | siehe "Offene Punkte" |
| Handy zeigt dauerhaft "Wird mit Android Auto verbunden", im Log kein `[WLAN]` | HeadUnit ist nicht im kabellosen Modus (die Kachel Android Auto ist USB) | `--wireless` oder **Android Auto kabellos** |
| Handy zeigt dauerhaft "Wird mit Android Auto verbunden" | Das Handy kommt nicht ins WLAN oder nicht zu Port 5288 | Log lesen (siehe unten): wie weit kam der Bluetooth-Dialog? |
| "Das Handy hat die WLAN-Daten bekommen, ist dem WLAN aber nicht beigetreten" | Falsches WLAN-Land, Handy ohne 5 GHz, WLAN am Handy aus | `HEADUNIT_WIFI_BAND=bg`; Log `[WLAN]` zeigt den Status des Handys |
| Handy ist im WLAN, Android Auto startet nicht | Port 5288 blockiert | `sudo ss -ltnp \| grep 5288` waehrend der Wartezeit; Firewall pruefen |

**Das Log lesen:** Jeder Schritt steht in `headunit.log` (im Ordner, aus dem HeadUnit gestartet wurde), Bluetooth unter
`[BT]`, der Dialog und das WLAN unter `[WLAN]`, die Sitzung unter `[AA]`:

```sh
grep -E '\[(BT|WLAN|AA)\]' headunit.log | tail -n 80
```

Die wichtigen Zeilen, in dieser Reihenfolge: "Hotspot ready after ... ms", "Android Auto Wireless service registered",
"Asking the paired phone ... to connect", "Connected = true", "opened the Android Auto Wireless service" (Handy hat den
Dienst geoeffnet), "Sent the start request", "The phone asked for the Wi-Fi details", "Wi-Fi details sent", "The phone
answered the start request (status 0 ...)", "The phone opened the wireless connection". Die letzte vorhandene Zeile
zeigt, wo es haengt.

## Offene Punkte (erst am Handy zu klaeren)

- **Freisprechen (HFP):** Ein echtes Auto bietet auch Freisprechen und Audio an. Auf Raspberry Pi OS mit Desktop
  meldet PipeWire diese Profile bereits an; das Handy hat HEATUNIT damit als Auto erkannt. Auf einem System ohne
  PipeWire koennte das fehlen.
- **Neu verbinden:** Ob das Trennen und Neuverbinden eines schon verbundenen Handys Android Auto zuverlaessig neu
  anstoesst, ist am Handy noch nicht bestaetigt. Sonst hilft von Hand: am Handy Bluetooth aus- und wieder einschalten,
  waehrend HeadUnit im kabellosen Modus ist.
- **5 GHz:** Manche Handys verlangen 5 GHz fuer kabelloses Android Auto. Der Pi 4 kann es; es braucht das WLAN-Land.
- **Reihenfolge der Nachrichten:** Der Ablauf reagiert auf das, was das Handy sendet, und protokolliert alles (auch
  unbekannte Nachrichten mit Id und Groesse). Weicht ein Handy ab, steht es im Log.
- **Mikrofon** ist wie bei USB noch nicht angebunden.
