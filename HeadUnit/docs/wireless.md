# Kabelloses Android Auto (Linux, Raspberry Pi)

Statt USB-Kabel: HeadUnit ist per Bluetooth als **HEATUNIT** sichtbar und erzeugt selbst ein WLAN. Das Handy wird
einmal gekoppelt, danach verbindet es sich von selbst. Nur Linux; unter Windows bleibt es beim Kabel.

**Stand der Pruefung:** Der Code ist geschrieben und die Teile ohne Handy sind getestet (Nachrichtenformat, Ablauf,
Socket-Transport: `ctest`). Bluetooth, Hotspot und das Zusammenspiel mit einem echten Handy sind noch **nicht**
ausprobiert worden. Deshalb gibt es drei Testschritte, die einzeln zeigen, wo etwas hakt (unten).

## Wie es funktioniert

```text
Handy                                      HeadUnit (Raspberry Pi)
  |  1. Bluetooth: koppeln (einmal)  --->   sichtbar als HEATUNIT, Kopplung ohne PIN
  |  2. Bluetooth: Dienst "Android Auto Wireless" (RFCOMM)
  |        <--- "verbinde dich mit 10.42.0.1:5288"
  |        ---> "welches WLAN?"
  |        <--- SSID, Passwort, BSSID (WPA2)
  |  3. WLAN: Handy tritt dem Hotspot HEATUNIT-AA bei
  |  4. TCP zu 10.42.0.1:5288  --->   dieselbe Android-Auto-Sitzung wie ueber USB
```

- Der **Hotspot kommt vom HeadUnit-Rechner**, nicht vom Handy. Er wird mit NetworkManager (`nmcli`) angelegt, der auch
  die Adressen verteilt (`ipv4.method shared`).
- **Bluetooth** dient nur dazu, das Handy zu finden und ihm die WLAN-Daten zu geben. HeadUnit spricht dafuer mit
  BlueZ ueber D-Bus (QtDBus, steckt in `qt6-base-dev`).
- Ab dem TCP-Anschluss laufen Protokoll, Video, Ton und Eingabe **unveraendert**: nur der Transport ist ein anderer
  (`SocketTransport` statt `ProjectionTransport`).

Code: `src/wireless/` (`WirelessProtocol` Nachrichten und Ablauf, `BluetoothService` BlueZ, `Hotspot` nmcli,
`SocketTransport` TCP, `WirelessConnect` der ganze Ablauf).

## Voraussetzungen

| | |
| --- | --- |
| Rechner | Raspberry Pi 4 (Bluetooth und WLAN eingebaut) mit Raspberry Pi OS Bookworm |
| Dienste | `NetworkManager` (Bookworm-Standard) und `bluetooth` (BlueZ); `systemctl status NetworkManager bluetooth` |
| WLAN-Land | muss gesetzt sein, sonst startet der 5-GHz-Hotspot nicht: `sudo raspi-config` → Localisation Options → WLAN Country |
| Bluetooth | nicht per rfkill gesperrt: `rfkill list`, sonst `sudo rfkill unblock bluetooth wifi` |
| Handy | Android Auto **mit kabellos-Unterstuetzung** (Android 11 oder neuer; bei manchen Handys ist es in den Android-Auto-Einstellungen abgeschaltet), WLAN und Bluetooth an, 5 GHz empfohlen |
| Bauen | nichts Neues: `qt6-base-dev` bringt QtDBus mit. Ohne QtDBus baut CMake ohne kabellos und warnt (`-DHEADUNIT_WIRELESS=OFF` schaltet es bewusst ab) |

**Achtung SSH:** Der WLAN-Chip des Pi wird zum Hotspot und kann dann kein WLAN-Client mehr sein. Eine SSH-Verbindung
**ueber WLAN bricht ab**, sobald der Hotspot startet. Zum Entwickeln ein Netzwerkkabel (Ethernet) verwenden. Nach dem
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

oder im Fenster auf **Android Auto kabellos** klicken. Ablauf: Hotspot starten, sichtbar werden, auf das Handy warten,
WLAN-Daten uebergeben, Handy tritt bei, Sitzung. Das Fenster zeigt jeden Schritt. Mit **Verbindung beenden** endet alles
und der Hotspot wird abgebaut. Aus BuildAndRun: `bash BuildAndRun.sh release -- --wireless`.

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
| "Der Bluetooth-Adapter hci0 laesst sich nicht einschalten" | Bluetooth-Dienst aus oder rfkill | `systemctl status bluetooth`, `sudo rfkill unblock bluetooth`, `bluetoothctl show` |
| "Der Bluetooth-Dienst fuer Android Auto laesst sich nicht anmelden" | Keine Rechte am System-D-Bus | Nutzer in die Gruppe `bluetooth`: `sudo usermod -aG bluetooth $USER`, neu anmelden |
| HEATUNIT erscheint am Handy nicht | Nicht sichtbar | `bluetoothctl show` (Discoverable: yes), Log `[BT]` |
| Kopplung klappt, aber nichts passiert | Das Handy oeffnet den Dienst nicht von selbst | siehe "Offene Punkte" |
| "Das Handy ist dem WLAN nicht beigetreten" | Falsches WLAN-Land, Handy ohne 5 GHz, WLAN am Handy aus | `HEADUNIT_WIFI_BAND=bg`; Log `[WLAN]` zeigt den Status des Handys |
| Handy ist im WLAN, Android Auto startet nicht | Port 5288 blockiert | `sudo ss -ltnp \| grep 5288` waehrend der Wartezeit; Firewall pruefen |

## Offene Punkte (erst am Handy zu klaeren)

- **Verbindet das Handy den Dienst von selbst?** Android startet kabelloses Android Auto, sobald es Bluetooth zu einem
  gekoppelten Auto aufbaut. Ein echtes Auto bietet dafuer auch Freisprechen (HFP) und Audio an; HeadUnit bietet bisher
  nur den Android-Auto-Dienst an. Falls das Handy nach dem Koppeln nichts tut, ist das der erste Verdacht: dann kommt
  ein HFP-Profil dazu (BlueZ bringt sein eigenes mit, das kann sich mit einem eigenen beissen, deshalb noch nicht
  eingebaut). Bis dahin hilft es, in den Android-Auto-Einstellungen das Auto manuell zu verbinden.
- **5 GHz:** Manche Handys verlangen 5 GHz fuer kabelloses Android Auto. Der Pi 4 kann es; es braucht das WLAN-Land.
- **Reihenfolge der Nachrichten:** Der Ablauf reagiert auf das, was das Handy sendet, und protokolliert alles (auch
  unbekannte Nachrichten mit Id und Groesse). Weicht ein Handy ab, steht es im Log.
- **Mikrofon** ist wie bei USB noch nicht angebunden.
