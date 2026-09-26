# Kabelloses Android Auto (Linux, Raspberry Pi)

HeadUnit ist immer bereit, wie ein Auto: Ab dem Programmstart ist es per **Bluetooth** als **HEATUNIT** sichtbar, ein
**verborgenes WLAN** laeuft im Hintergrund, und gleichzeitig wird der USB-Anschluss beobachtet. Am Handy gibt es nur
eines zu tun: in den Bluetooth-Einstellungen HEATUNIT waehlen und den Code bestaetigen. Danach fragt das Handy selbst,
ob es Android Auto verwenden soll, und verbindet sich kuenftig von allein. Ein WLAN muss man nie auswaehlen. Welches
Handy zuerst kommt, per Kabel oder kabellos, bekommt Android Auto. Kabellos nur unter Linux; unter Windows gibt es nur
die automatische USB-Erkennung.

**Stand der Pruefung:** Auf dem Raspberry Pi 4 mit einem echten Handy (Samsung SM-F776B): Die erste Kopplung klappte
(damals noch ohne Code), nach dem Entkoppeln am Handy meldete es beim neuen Versuch aber "Keine Kopplung durchgefuehrt".
Ursache: Der Pi kannte das Handy noch, und BlueZ lehnt eine Kopplung ohne Code (Just Works) mit einem schon bekannten
Geraet ab (`JustWorksRepairing = never`). Seit 2026-09-27 koppelt HeadUnit mit Codevergleich wie ein Auto, und
Bluetooth antwortet auf einem eigenen Thread (vorher nur zwischen zwei Runden der Automatik). Am Handy noch nicht
bestaetigt. Ohne Handy getestet (`ctest`): Nachrichtenformat, der Bluetooth-Dialog gegen ein simuliertes Handy,
Socket-Transport, der Ablauf der Automatik.

## Wie es funktioniert

```text
Handy                                      HeadUnit (Raspberry Pi), ab Programmstart
                                            Bluetooth an (rfkill entsperren), Dienst "Android Auto Wireless"
                                            auf RFCOMM-Kanal 8, sichtbar als HEATUNIT (1 bis 2 Sekunden)
                                            gleichzeitig im Hintergrund: verborgenes WLAN HEATUNIT-AA (einige Sekunden)
  |  1. Bluetooth: HEATUNIT koppeln (einmal) ---> Handy und HeadUnit zeigen denselben 6-stelligen Code;
  |                                              HeadUnit bestaetigt selbst, am Handy "Koppeln" tippen
  |        <--- WLAN laeuft: gekoppelte Handys verbinden (ein schon verbundenes Handy zuerst trennen)
  |  2. Android Auto am Handy: "Android Auto verwenden?"; es oeffnet den Dienst "Android Auto Wireless" (RFCOMM)
  |        <--- sofort (laeuft das WLAN noch nicht, sobald es laeuft): "verbinde dich mit 10.42.0.1:5288"
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
  WLAN-Liste auf, das Handy bekommt Name und Passwort ueber Bluetooth. Er startet gleichzeitig mit Bluetooth im
  Hintergrund (`nmcli` braucht einige Sekunden), damit HEATUNIT sofort sichtbar ist; oeffnet ein Handy den
  Android-Auto-Dienst, bevor er laeuft, wartet HeadUnit auf ihn (bis 60 Sekunden). Ein Hotspot, den ein abgebrochener
  Lauf hinterlassen hat, wird beim naechsten Start entfernt.
- **Verborgen, und wenn das nicht geht, sichtbar:** Hat ein Handy die WLAN-Daten bekommen, ist dem verborgenen WLAN
  aber nicht beigetreten, macht HeadUnit das WLAN ab dann sichtbar (startet es dafuer neu) und merkt sich das
  (`wireless/wifiVisible` in `~/.config/HeadUnit/HeadUnit.conf`). Auswaehlen muss man es trotzdem nie; das Handy tritt
  von selbst bei. `HEADUNIT_WIFI_HIDDEN=1` haelt es immer verborgen, `=0` immer sichtbar.
- **Bluetooth** dient dazu, das Handy zu finden und ihm die WLAN-Daten zu geben. HeadUnit spricht dafuer mit
  BlueZ ueber D-Bus (QtDBus, steckt in `qt6-base-dev`) und schaltet es selbst ein: Adapter einschalten, und wenn er
  per rfkill gesperrt ist (Raspberry Pi OS macht das), zuerst entsperren (ueber `/dev/rfkill`, sonst `sudo -n rfkill`).
  Sichtbar wird HEATUNIT erst, wenn Kopplungsagent und Android-Auto-Dienst angemeldet sind; so findet ein Handy, das
  sofort koppelt, den Dienst schon und bietet Android Auto an. Bluetooth laeuft auf einem **eigenen Thread**: Kopplung
  und Verbindungen werden sofort beantwortet, auch waehrend einer Sitzung oder waehrend der Hotspot startet.
- **Kopplung wie im Auto (Codevergleich):** HeadUnit meldet sich bei BlueZ als Geraet mit Anzeige und Ja/Nein-Taste
  (`DisplayYesNo`). Das Handy zeigt einen 6-stelligen Code, HeadUnit zeigt denselben im Fenster ("Bluetooth-Kopplung
  mit ...: am Handy den Code ... bestaetigen") und bestaetigt selbst; am Handy "Koppeln" tippen. Vorher war es eine
  Kopplung ohne Code (Just Works). Die lehnt BlueZ ab, wenn es das Handy noch von einer frueheren Kopplung kennt
  (`JustWorksRepairing = never`, Standard): Wer am Handy entkoppelt und neu koppeln wollte, bekam "Keine Kopplung
  durchgefuehrt". Beim Codevergleich gilt diese Regel nicht.
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

Macht den Pi 120 Sekunden lang als **HEATUNIT** sichtbar. Am Handy: Einstellungen → Bluetooth → HEATUNIT koppeln; der
Code, den das Handy zeigt, steht auch im Terminal ("Bluetooth-Kopplung mit ...: am Handy den Code ... bestaetigen").
Besteht der Test, hat das Handy den Dienst geoeffnet, die WLAN-Daten erfragt und bekommen. **`headunit.log` zeigt jeden
Schritt** (`[BT]`: Kopplung, `Connected = true`, "opened the Android Auto Wireless service"; `[WLAN]`: die Nachrichten
des Handys).

**3. Alles zusammen:** einfach starten.

```sh
./HeadUnit
```

Aus BuildAndRun: `bash BuildAndRun.sh release`. Das Fenster zeigt jeden Schritt: "Bluetooth sichtbar als 'HEATUNIT'",
"Kabellos bereit: ... WLAN ... laeuft", beim Koppeln den Code, dann, sobald das Handy kommt, "Handy verbindet sich
kabellos", "Handy im WLAN verbunden", die Sitzung. `--wireless` wird noch angenommen, aendert aber nichts mehr.

**Wurde HEATUNIT frueher schon gekoppelt** (mit einer aelteren Version), am Handy HEATUNIT einmal entkoppeln
("Entkoppeln"/"Vergessen") und neu koppeln; das klappt jetzt auch, wenn der Pi das Handy noch kennt. Wer ganz sauber
anfangen will, entfernt es auch am Pi: `bluetoothctl devices`, dann `bluetoothctl remove <Adresse>`.

## Einstellungen (Umgebungsvariablen)

| Variable | Bedeutung | Standard |
| --- | --- | --- |
| `HEADUNIT_BT_NAME` | Bluetooth-Name | `HEATUNIT` |
| `HEADUNIT_WIFI_SSID` | Name des Hotspots | `HEATUNIT-AA` |
| `HEADUNIT_WIFI_PASSWORD` | Passwort des Hotspots (8 bis 63 Zeichen) | 16 zufaellige Zeichen, gespeichert in `~/.config/HeadUnit/HeadUnit.conf` |
| `HEADUNIT_WIFI_HIDDEN` | `0` = immer sichtbar, `1` = immer verborgen | verborgen; sichtbar, sobald ein Handy dem verborgenen WLAN nicht beitrat |
| `HEADUNIT_WIFI_INTERFACE` | WLAN-Geraet | das erste, das NetworkManager kennt (`wlan0`) |
| `HEADUNIT_WIFI_BAND` | `a` = 5 GHz, `bg` = 2,4 GHz | `a` |
| `HEADUNIT_WIFI_CHANNEL` | Kanal | 36 (`a`) bzw. 6 (`bg`) |
| `HEADUNIT_TEST_SECONDS` | Dauer von `--test-bluetooth` / `--test-hotspot` | 120 / 60 |

Das Passwort muss der Nutzer nie eintippen: das Handy bekommt es ueber Bluetooth. Es bleibt gleich, damit das Handy
sein gemerktes WLAN weiter benutzen kann.

## Sicherheit

Solange HeadUnit laeuft, ist der Pi per Bluetooth **dauerhaft sichtbar** und **bestaetigt jede Kopplung selbst** (wie
ein Auto im Kopplungsmodus, nur ohne Tippen am Bildschirm): Jedes Handy in Reichweite kann sich koppeln; bestaetigt
wird am Handy, der Code steht zur Kontrolle im Fenster. Der Hotspot ist mit WPA2 geschuetzt und normalerweise
verborgen; er hat keinen Internetzugang und ist nur fuer das Handy gedacht.

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
| Am Handy "Keine Kopplung durchgefuehrt" | Kopplung abgelehnt oder zu spaet beantwortet | Log `[BT]`: steht "pairing with code comparison" da und kommt "Pairing with ... confirmed"? Wenn nicht: am Pi `bluetoothctl remove <Adresse>` und neu koppeln |
| "Wird mit Android Auto verbunden" bleibt, im Log kein "opened the Android Auto Wireless service" | Das Handy erreicht den Dienst nicht | Log: steht "RFCOMM channel 8" da? `bluetoothctl show` listet die UUID `4de17a00-...`; am Handy HEATUNIT entkoppeln und neu koppeln |
| "Das Handy hat die WLAN-Daten bekommen, ist dem WLAN aber nicht beigetreten" | Verborgenes WLAN nicht gefunden, falsches WLAN-Land, Handy ohne 5 GHz, WLAN am Handy aus | HeadUnit macht das WLAN danach selbst sichtbar; klappt es dann auch nicht: `HEADUNIT_WIFI_BAND=bg`; Log `[WLAN]` zeigt den Status des Handys |
| Handy ist im WLAN, Android Auto startet nicht | Port 5288 blockiert | `sudo ss -ltnp \| grep 5288`; Firewall pruefen |
| Handy am USB-Kabel startet nicht erneut | Absicht: ein Handy bekommt einen Versuch pro Anstecken | Kabel neu anstecken oder **Android Auto verbinden** |

**Das Log lesen:** Jeder Schritt steht in `headunit.log` (im Ordner, aus dem HeadUnit gestartet wurde), die Automatik
unter `[WATCH]`, Bluetooth unter `[BT]`, der Dialog und das WLAN unter `[WLAN]`, die Sitzung unter `[AA]`:

```sh
grep -E '\[(WATCH|BT|WLAN|AA)\]' headunit.log | tail -n 80
```

Die wichtigen Zeilen, in dieser Reihenfolge: "service registered on RFCOMM channel 8", "Hotspot ready after ... ms",
beim ersten Mal "Pairing with ... confirmed, code ..." und "Paired = true", "Asking the paired phone ... to connect", "Connected = true", "opened the Android Auto Wireless service" (Handy hat den
Dienst geoeffnet), "Sent the start request", "The phone asked for the Wi-Fi details", "Wi-Fi details sent", "The phone
answered the start request (status 0 ...)", "The phone opened the wireless connection". Die letzte vorhandene Zeile
zeigt, wo es haengt.

## Offene Punkte (erst am Handy zu klaeren)

- **Kopplung mit Codevergleich:** am Handy noch nicht bestaetigt (der Grund fuer "Keine Kopplung durchgefuehrt" ist aus
  BlueZ' Regeln abgeleitet, nicht im Log des Pi gesehen).
- **Verborgenes WLAN:** Ob Android Auto einem verborgenen Netz beitritt, ist am Handy noch nicht bestaetigt. Tritt das
  Handy nicht bei, schaltet HeadUnit von selbst auf sichtbar um (siehe oben); der naechste Versuch kommt dann vom Handy
  oder vom Neuverbinden nach dem WLAN-Neustart.
- **Neu verbinden:** Ob das Trennen und Neuverbinden eines schon verbundenen Handys Android Auto zuverlaessig neu
  anstoesst, ist noch nicht bestaetigt. Sonst hilft von Hand: am Handy Bluetooth aus- und wieder einschalten.
- **Freisprechen (HFP):** Ein echtes Auto bietet auch Freisprechen und Audio an. Auf Raspberry Pi OS mit Desktop
  meldet PipeWire diese Profile bereits an; das Handy hat HEATUNIT damit als Auto erkannt. Auf einem System ohne
  PipeWire koennte das fehlen.
- **5 GHz:** Manche Handys verlangen 5 GHz fuer kabelloses Android Auto. Der Pi 4 kann es; es braucht das WLAN-Land.
- **Reihenfolge der Nachrichten:** Der Ablauf reagiert auf das, was das Handy sendet, und protokolliert alles (auch
  unbekannte Nachrichten mit Id und Groesse). Weicht ein Handy ab, steht es im Log.
- **Mikrofon** ist wie bei USB noch nicht angebunden.
