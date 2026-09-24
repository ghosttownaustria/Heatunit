# Kabelloses Android Auto (Linux, Raspberry Pi)

HeadUnit ist immer bereit, wie ein Auto: Ab dem Programmstart laufen ein **verborgenes WLAN** und **Bluetooth**
(sichtbar als **HEATUNIT**), und gleichzeitig wird der USB-Anschluss beobachtet. Welches Handy zuerst kommt, per Kabel
oder kabellos, bekommt Android Auto. Gedrueckt werden muss nichts. Das Handy wird einmal gekoppelt, danach verbindet es
sich von selbst. Kabellos nur unter Linux; unter Windows gibt es nur die automatische USB-Erkennung.

**Stand der Pruefung:** Auf dem Raspberry Pi 4 mit einem echten Handy (Samsung SM-F776B): Kopplung klappt, das Handy
ist "fuer Anrufe und Audio verbunden" und zeigt "Wird mit Android Auto verbunden", trat dem WLAN aber nie bei. Ursache:
Der Android-Auto-Dienst wurde ohne RFCOMM-Kanal bei BlueZ angemeldet. BlueZ oeffnet fuer eine ihm unbekannte UUID dann
**gar keinen RFCOMM-Server**; das Handy sieht den Dienst, kann sich aber nie verbinden. Jetzt: Kanal 8 (wie die
kabellosen Adapter, die mit echten Handys laufen). Am Handy noch nicht bestaetigt. Ohne Handy getestet (`ctest`):
Nachrichtenformat, der Bluetooth-Dialog gegen ein simuliertes Handy, Socket-Transport, der Ablauf der Automatik.

## Wie es funktioniert

```text
Handy                                      HeadUnit (Raspberry Pi), ab Programmstart
                                            verborgenes WLAN HEATUNIT-AA starten (einige Sekunden)
                                            Bluetooth an (rfkill entsperren), sichtbar als HEATUNIT,
                                            Dienst "Android Auto Wireless" auf RFCOMM-Kanal 8
  |  1. Bluetooth: koppeln (einmal)  --->   Kopplung ohne PIN, Handy wird "vertraut"
  |        <--- gekoppelte Handys verbinden (ein schon verbundenes Handy zuerst trennen)
  |  2. Bluetooth: Dienst "Android Auto Wireless" (RFCOMM)
  |        <--- sofort: "verbinde dich mit 10.42.0.1:5288"
  |        ---> "welches WLAN?"
  |        <--- SSID, Passwort, BSSID, WPA2
  |  3. WLAN: Handy tritt dem Hotspot HEATUNIT-AA bei (es kennt den Namen aus Schritt 2)
  |  4. TCP zu 10.42.0.1:5288  --->   dieselbe Android-Auto-Sitzung wie ueber USB
  |                                         Sitzung vorbei: WLAN und Bluetooth bleiben, warten auf das naechste Mal
```

- **Automatik** (`RunPhoneWatch`): Ein Hintergrund-Ablauf schaut jede Sekunde auf den USB-Bus und wartet dazwischen
  auf Bluetooth. Ein Handy am USB-Kabel bekommt **einmal** Android Auto, wenn es angesteckt wird (auch wenn es beim
  Start schon steckt), und erst nach dem Abziehen wieder. So startet ein Handy, bei dem die Sitzung beendet wurde, nicht
  sofort neu. Nochmals ohne Abziehen: **Android Auto verbinden** (oder die Kachel Android Auto). Ein kabelloses Handy
  kann sich jederzeit selbst neu verbinden. Wird ein Handy waehrend einer kabellosen Sitzung zum Laden angesteckt,
  uebernimmt USB danach nicht.
- **Verbindung beenden** beendet nur die laufende Sitzung; die Automatik laeuft weiter. Erst das Schliessen des
  Fensters (oder Ctrl+C, `systemctl stop`) baut WLAN und Bluetooth ab.
- Der **Hotspot kommt vom HeadUnit-Rechner**, nicht vom Handy. Er wird mit NetworkManager (`nmcli`) angelegt, der auch
  die Adressen verteilt (`ipv4.method shared`), und ist **verborgen** (`802-11-wireless.hidden`): Er taucht in keiner
  WLAN-Liste auf, das Handy bekommt Name und Passwort ueber Bluetooth. Er laeuft vor Bluetooth an, weil das Handy die
  erste Nachricht erwartet, sobald es den Android-Auto-Dienst geoeffnet hat. Ein Hotspot, den ein abgebrochener Lauf
  hinterlassen hat, wird beim naechsten Start entfernt.
- **Bluetooth** dient nur dazu, das Handy zu finden und ihm die WLAN-Daten zu geben. HeadUnit spricht dafuer mit
  BlueZ ueber D-Bus (QtDBus, steckt in `qt6-base-dev`) und schaltet es selbst ein: Adapter einschalten, und wenn er
  per rfkill gesperrt ist (Raspberry Pi OS macht das), zuerst entsperren (ueber `/dev/rfkill`, sonst `sudo -n rfkill`).
- **RFCOMM-Kanal:** BlueZ oeffnet fuer einen angemeldeten Dienst nur dann einen RFCOMM-Server, wenn es einen Kanal
  bekommt oder die UUID aus seiner eigenen Liste kennt. Die Android-Auto-UUID kennt es nicht; HeadUnit gibt deshalb
  Kanal 8 an (`kAndroidAutoWirelessChannel`). Das Log sagt es: "service registered on RFCOMM channel 8".
- **Schon verbundenes Handy:** PipeWire verbindet ein gekoppeltes Handy "fuer Anrufe und Audio" von selbst, schon bevor
  HeadUnit laeuft. Android Auto sucht seinen Dienst beim Verbinden; gibt es ihn da noch nicht, bleibt "Wird mit Android
  Auto verbunden" stehen. Darum trennt HeadUnit, sobald alles bereit ist, jedes schon verbundene Handy und verbindet es
  neu (nur Geraete, die BlueZ als Handy fuehrt; Log `[BT]`: "reconnecting it").
- **Klappt der Start nicht** (Bluetooth beim Hochfahren noch nicht da, NetworkManager fehlt, ...), versucht HeadUnit es
  jede Minute erneut. USB funktioniert in der Zeit weiter.
- **Sicherheitsmodus:** Das Handy liest ihn in Androids eigener Zaehlung (WPA 4, WPA2 8, beides 12). Die importierte
  Aufzaehlung `WifiSecurityMode` zaehlt 0 bis 9; ihr `WPA2_PERSONAL` (5) kennt das Handy nicht und tritt dem WLAN dann
  nie bei. HeadUnit sendet deshalb 8 (`kWifiSecurityWpa2Personal`).
- Ab dem TCP-Anschluss laufen Protokoll, Video, Ton und Eingabe **unveraendert**: nur der Transport ist ein anderer
  (`SocketTransport` statt `ProjectionTransport`).

Code: `src/androidauto/PhoneWatch` (die Automatik, ohne Qt und getestet), `src/wireless/` (`WirelessProtocol`
Nachrichten, `WirelessLink` der Bluetooth-Dialog bis zur TCP-Verbindung, `BluetoothService` BlueZ, `Hotspot` nmcli,
`SocketTransport` TCP, `WirelessConnect` die `WirelessStation`, die WLAN und Bluetooth am Laufen haelt).

## Voraussetzungen

| | |
| --- | --- |
| Rechner | Raspberry Pi 4 (Bluetooth und WLAN eingebaut) mit Raspberry Pi OS Bookworm |
| Dienste | `NetworkManager` (Bookworm-Standard) und `bluetooth` (BlueZ); `systemctl status NetworkManager bluetooth` |
| WLAN-Land | muss gesetzt sein, sonst startet der 5-GHz-Hotspot nicht: `sudo raspi-config` → Localisation Options → WLAN Country |
| Bluetooth | schaltet HeadUnit selbst ein. Nur bei einer Sperre per Hardware (`rfkill list`: "Hard blocked: yes") geht das nicht |
| Handy | Android Auto **mit kabellos-Unterstuetzung** (Android 11 oder neuer; bei manchen Handys ist es in den Android-Auto-Einstellungen abgeschaltet), WLAN und Bluetooth an, 5 GHz empfohlen |
| Bauen | nichts Neues: `qt6-base-dev` bringt QtDBus mit. Ohne QtDBus baut CMake ohne kabellos und warnt (`-DHEADUNIT_WIRELESS=OFF` schaltet es bewusst ab) |

**Achtung WLAN:** Der WLAN-Chip des Pi ist Hotspot, **solange HeadUnit laeuft**, und kann dann kein WLAN-Client sein:
Eine SSH-Verbindung ueber WLAN bricht beim Start von HeadUnit ab, und Internet ueber WLAN (Internetradio) gibt es in der
Zeit nicht. Zum Entwickeln ein Netzwerkkabel (Ethernet) verwenden. Nach dem Beenden verbindet sich der Pi wieder mit
seinem WLAN.

## Ausprobieren, Schritt fuer Schritt

Jeweils aus `HeadUnit/out/build/linux-release` (oder `-debug`), nach `bash BuildAndRun.sh --no-run`.

**1. Hotspot** (ohne Handy-Zusammenspiel):

```sh
./HeadUnit --test-hotspot
```

Gibt Netzname, Passwort und Adresse aus und haelt den Hotspot 60 Sekunden (`HEADUNIT_TEST_SECONDS=300`). Am Handy
manuell beitreten: Weil das Netz verborgen ist, in den WLAN-Einstellungen "Netzwerk hinzufuegen" waehlen und den Namen
eintippen (oder fuer den Test sichtbar: `HEADUNIT_WIFI_HIDDEN=0 ./HeadUnit --test-hotspot`). Klappt das, ist der
Hotspot in Ordnung.

**2. Bluetooth und Kopplung:**

```sh
./HeadUnit --test-bluetooth
```

Macht den Pi 120 Sekunden lang als **HEATUNIT** sichtbar. Am Handy: Einstellungen → Bluetooth → HEATUNIT koppeln.
Besteht der Test, hat das Handy den Dienst geoeffnet, die WLAN-Daten erfragt und bekommen. **`headunit.log` zeigt jeden
Schritt** (`[BT]`: Kopplung, `Connected = true`, "opened the Android Auto Wireless service"; `[WLAN]`: die Nachrichten
des Handys).

**3. Alles zusammen:** einfach starten.

```sh
./HeadUnit
```

Aus BuildAndRun: `bash BuildAndRun.sh release`. Das Fenster zeigt jeden Schritt: "Kabellos bereit: WLAN ... laeuft",
dann, sobald das Handy kommt, "Handy verbindet sich kabellos", "Handy im WLAN verbunden", die Sitzung. `--wireless`
wird noch angenommen, aendert aber nichts mehr.

**Wurde HEATUNIT frueher schon gekoppelt** (mit einer Version ohne RFCOMM-Kanal), hat sich das Handy den alten Dienst
womoeglich gemerkt. Klappt es nicht auf Anhieb: am Handy HEATUNIT entkoppeln ("Entkoppeln"/"Vergessen") und neu koppeln.

## Einstellungen (Umgebungsvariablen)

| Variable | Bedeutung | Standard |
| --- | --- | --- |
| `HEADUNIT_BT_NAME` | Bluetooth-Name | `HEATUNIT` |
| `HEADUNIT_WIFI_SSID` | Name des Hotspots | `HEATUNIT-AA` |
| `HEADUNIT_WIFI_PASSWORD` | Passwort des Hotspots (8 bis 63 Zeichen) | 16 zufaellige Zeichen, gespeichert in `~/.config/HeadUnit/HeadUnit.conf` |
| `HEADUNIT_WIFI_HIDDEN` | `0` = der Netzname wird gesendet (sichtbares WLAN) | verborgen |
| `HEADUNIT_WIFI_INTERFACE` | WLAN-Geraet | das erste, das NetworkManager kennt (`wlan0`) |
| `HEADUNIT_WIFI_BAND` | `a` = 5 GHz, `bg` = 2,4 GHz | `a` |
| `HEADUNIT_WIFI_CHANNEL` | Kanal | 36 (`a`) bzw. 6 (`bg`) |
| `HEADUNIT_TEST_SECONDS` | Dauer von `--test-bluetooth` / `--test-hotspot` | 120 / 60 |

Das Passwort muss der Nutzer nie eintippen: das Handy bekommt es ueber Bluetooth. Es bleibt gleich, damit das Handy
sein gemerktes WLAN weiter benutzen kann.

## Sicherheit

Solange HeadUnit laeuft, ist der Pi per Bluetooth **dauerhaft sichtbar** und **koppelt ohne PIN** (wie ein Auto im
Kopplungsmodus): Jedes Handy in Reichweite kann sich koppeln. Das Handy bestaetigt die Kopplung selbst. Der Hotspot ist
mit WPA2 geschuetzt und verborgen; er hat keinen Internetzugang und ist nur fuer das Handy gedacht.

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
| "Wird mit Android Auto verbunden" bleibt, im Log kein "opened the Android Auto Wireless service" | Das Handy erreicht den Dienst nicht | Log: steht "RFCOMM channel 8" da? `bluetoothctl show` listet die UUID `4de17a00-...`; am Handy HEATUNIT entkoppeln und neu koppeln |
| "Das Handy hat die WLAN-Daten bekommen, ist dem WLAN aber nicht beigetreten" | Verborgenes WLAN nicht gefunden, falsches WLAN-Land, Handy ohne 5 GHz, WLAN am Handy aus | zuerst `HEADUNIT_WIFI_HIDDEN=0`, dann `HEADUNIT_WIFI_BAND=bg`; Log `[WLAN]` zeigt den Status des Handys |
| Handy ist im WLAN, Android Auto startet nicht | Port 5288 blockiert | `sudo ss -ltnp \| grep 5288`; Firewall pruefen |
| Handy am USB-Kabel startet nicht erneut | Absicht: ein Handy bekommt einen Versuch pro Anstecken | Kabel neu anstecken oder **Android Auto verbinden** |

**Das Log lesen:** Jeder Schritt steht in `headunit.log` (im Ordner, aus dem HeadUnit gestartet wurde), die Automatik
unter `[WATCH]`, Bluetooth unter `[BT]`, der Dialog und das WLAN unter `[WLAN]`, die Sitzung unter `[AA]`:

```sh
grep -E '\[(WATCH|BT|WLAN|AA)\]' headunit.log | tail -n 80
```

Die wichtigen Zeilen, in dieser Reihenfolge: "Hotspot ready after ... ms", "service registered on RFCOMM channel 8",
"Asking the paired phone ... to connect", "Connected = true", "opened the Android Auto Wireless service" (Handy hat den
Dienst geoeffnet), "Sent the start request", "The phone asked for the Wi-Fi details", "Wi-Fi details sent", "The phone
answered the start request (status 0 ...)", "The phone opened the wireless connection". Die letzte vorhandene Zeile
zeigt, wo es haengt.

## Offene Punkte (erst am Handy zu klaeren)

- **Verborgenes WLAN:** Ob Android Auto einem verborgenen Netz beitritt, ist am Handy noch nicht bestaetigt. Findet
  das Handy es nicht (Log: WLAN-Daten gesendet, aber kein Beitritt), mit `HEADUNIT_WIFI_HIDDEN=0` sichtbar machen.
- **Neu verbinden:** Ob das Trennen und Neuverbinden eines schon verbundenen Handys Android Auto zuverlaessig neu
  anstoesst, ist noch nicht bestaetigt. Sonst hilft von Hand: am Handy Bluetooth aus- und wieder einschalten.
- **Bluetooth waehrend einer Sitzung:** Automatik, Bluetooth und Sitzung teilen sich einen Ablauf. Waehrend eine Sitzung
  laeuft, beantwortet HeadUnit Anfragen von BlueZ erst danach; ein neues Handy koppelt sich also am besten, wenn keine
  Sitzung laeuft.
- **Freisprechen (HFP):** Ein echtes Auto bietet auch Freisprechen und Audio an. Auf Raspberry Pi OS mit Desktop
  meldet PipeWire diese Profile bereits an; das Handy hat HEATUNIT damit als Auto erkannt. Auf einem System ohne
  PipeWire koennte das fehlen.
- **5 GHz:** Manche Handys verlangen 5 GHz fuer kabelloses Android Auto. Der Pi 4 kann es; es braucht das WLAN-Land.
- **Reihenfolge der Nachrichten:** Der Ablauf reagiert auf das, was das Handy sendet, und protokolliert alles (auch
  unbekannte Nachrichten mit Id und Groesse). Weicht ein Handy ab, steht es im Log.
- **Mikrofon** ist wie bei USB noch nicht angebunden.
