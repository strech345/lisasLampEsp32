# WLAN und Uhrzeit

_Stand: November 2026_

Die Lampe braucht die Uhrzeit für Wecker und Feier-Modus. Sie kommt bevorzugt per **NTP** über ein
externes WLAN; ohne WLAN helfen **gespeicherte Zeit**, **TSF-Fallback** (WLAN-Beacons) und optional
**RDS** (UKW-Radio). Code: `src/wifi_controller.cpp`, `src/captive_portal.cpp`, `src/tsf_clock.cpp`,
`src/rds_clock.cpp`.

---

## WLAN-Modi

| Modus | Wann | Hotspot sichtbar |
|---|---|---|
| **STA** | Heimnetz nur für den NTP-Abgleich: Einschalten/Aufwachen und automatisch (Standard alle 5 h, einstellbar; nur mit eingetragenem Heim-WLAN) | nein |
| **AP** | Hotspot: langer Tastendruck (2 s) | ja |
| **APSTA** | beides gleichzeitig: Hotspot läuft und ein Abgleich ist fällig, oder langer Tastendruck während eines Abgleichs | ja |

Hotspot und Heimnetz sind **zwei getrennte Modi**, die gleichzeitig laufen dürfen. `startWifi(true)` schaltet
nur den Hotspot ein, `startWifi(false)` nur das Heimnetz; der jeweils andere Modus bleibt, wie er ist.
Ebenso schalten `apOff()` / `staOff()` nur ihren Modus ab (APSTA → STA bzw. APSTA → AP). Der Hotspot startet
**nur** per langem Tastendruck, ein Hotspot-Start verbindet nicht mit dem Heimnetz. Ohne Heim-WLAN bleibt das
WLAN nach dem Start aus.

Nach dem Speichern der WLAN-Einstellungen startet nur der betroffene Modus neu (`web_commands.cpp`):
geänderter Hotspot-Name/-Passwort → `restartAccessPoint()` (nur wenn er läuft); geänderte externe WLANs →
`restartStaSync()` (neuer Abgleich mit den neuen Daten).

| Einstellung | Wert |
|---|---|
| Hotspot-IP | `4.3.2.1` (öffentlicher Bereich, Samsung-Geräte akzeptieren sonst das Captive Portal nicht) |
| Max. Geräte im Hotspot | 4 |
| Sendeleistung / Bandbreite | 17 dBm für Hotspot **und** Heimnetz (`WIFI_TX_POWER`, in `startAccessPoint()` und `beginSta()`) / HT40, WLAN-Schlafmodus aus (aus Messungen ermittelt). Mit der Standardleistung scheitert die Anmeldung am Router mit Grund 2 (Anmeldung abgelaufen) |
| Kanal | beim Einschalten der beste aus 1/6/11 per Scan; im RTC-Speicher gemerkt → nach Deep Sleep kein Scan |
| Standard-Zugangsdaten | aus `include/secrets.h` (siehe [README](../README.md)) |

### Automatischer Start und Stopp

Die drei Zeiten stehen in `SystemSettings` und sind in der Weboberfläche einstellbar: Hotspot-Laufzeit
unter **WLAN → Hotspot**, die beiden Intervalle unter **Synchronisation**. Sie wirken ab der
nächsten Prüfung, ohne WLAN-Neustart. Den Stand (letzte Abgleiche je Quelle, nächster Abgleich) zeigt der
**Systemmonitor** in der Gruppe *Uhr*.

| Einstellung (`SystemSettings`) | Standard | Bereich | Wirkung |
|---|---|---|---|
| `apTimeoutMin` | 10 min | 0–1440 | Hotspot-Laufzeit ohne Bedienung; 0 = unbegrenzt |
| `syncIntervalMin` | 300 min (5 h) | 5–720 | nächster Zeitabgleich, wenn der letzte geklappt hat |
| `syncRetryMin` | 300 min (5 h) | 5–720 | nächster Versuch, wenn der letzte fehlgeschlagen ist |

`checkWifiStop()` prüft beide Modi getrennt:

- **Hotspot ohne Gerät:** 1 min kein Gerät im Hotspot → Hotspot aus (`STOP_WIFI_AFTER_MS`); ein laufender
  Abgleich übers Heimnetz bleibt.
- **Heimnetz-Zeitlimit:** Ein Abgleich, der nach 1 min (`STA_SYNC_TIMEOUT_MS`, gezählt ab `beginSta()`) weder
  geklappt noch aufgegeben hat, gilt als gescheitert (`syncFailed()`, siehe unten). Gilt nicht während
  WLAN-Suche/-Test und nicht, solange nach Erfolg noch der TSF-Snapshot läuft.
- **TSF bei jedem Sync-Versuch:** Erfolg → TSF-Snapshot (neue Bezugspunkte), danach `dropStaAfterSync()`.
  Fehlschlag (Fehlversuche oder Zeitlimit) → `syncFailed()`: Verbindungsversuch beenden, Funkteil im
  STA-Modus lassen, `requestTsfFallback()` (Uhrzeit aus den Bezugspunkten, neu gestartete APs neu
  basieren), erst danach schaltet `dropStaAfterFail()` das Heimnetz ab (Hotspot bleibt). Ist TSF aus oder
  die Liste leer, geht das Heimnetz sofort aus.
- **Hotspot-Laufzeit:** `apTimeoutMin` ohne Bedienung → Hotspot aus, auch wenn ein Gerät verbunden bleibt.
  Handys bleiben oft im Hintergrund verbunden; ohne dieses Limit liefe der Hotspot ewig und verhinderte
  den Deep Sleep. Jede echte Anfrage an die Weboberfläche startet die Laufzeit neu (`noteWebActivity()`
  in `captive_portal.cpp`); automatisches Nachfragen der geöffneten Seite (`/ping`, `/get_event_log`,
  `/get_rds_clock`) und Captive-Portal-Prüfungen zählen nicht. Bei **0** gelten weder die Laufzeit noch
  die 1-min-Regel für den Hotspot: er bleibt an, bis er per Drehknopf ausgeschaltet wird — solange gibt
  es keinen Deep Sleep.
- **Zeitabgleich (unabhängig vom Hotspot):** `checkWifiStart()` ist der einzige Zeitgeber fürs Heimnetz.
  `nextSyncIntervalMs()` nach dem Ende der letzten Heimnetz-Phase (STA aus) startet einen neuen Versuch:
  WLAN aus → nur STA; Hotspot läuft → STA wird dazugeschaltet (APSTA), der Hotspot bleibt. Während WLAN-Suche/-Test ruht er. Ob der letzte Versuch geklappt hat, merkt sich `s_lastSyncOk`: `beginSta()` setzt
  es auf „fehlgeschlagen“, erst die NTP-Antwort auf „erfolgreich“. Danach gilt `syncIntervalMin` bzw.
  `syncRetryMin`; ohne eingetragenes Heim-WLAN immer `syncIntervalMin`.
- **Deep Sleep:** `checkToGoSleep()` schläft höchstens `nextSyncIntervalMs()`; nach dem Timer-Wakeup
  startet `setup()` den STA-Abgleich. Der RTC-Drift-Vorlauf (5 min + Drift über die Schlafdauer) gilt nur
  für Wecker und Party-Tage. Ein langes Intervall bedeutet mehr Drift zwischen zwei Abgleichen (interner
  RC-Takt ca. 1,5 %: 12 h ≈ 11 min); das Maximum von 720 min hält außerdem die NTP-Zeit unter der
  24-h-Grenze, ab der sie als veraltet gilt.
- **Heimnetz nur für NTP:** Nach erfolgreichem NTP-Abgleich (und dem anschließenden TSF-Snapshot) trennt
  `dropStaAfterSync()` die Heimnetz-Verbindung: APSTA → nur Hotspot, STA-only → WLAN aus. Grund: Der
  ESP32-C3 hat ein Funkteil; mit STA muss der Hotspot dem Router-Kanal folgen und hakt bei jedem
  Verbindungsversuch. Die Weboberfläche ist daher nur über den Hotspot (`4.3.2.1`) erreichbar.
- Der Webserver wird nur einmal gestartet und läuft weiter; nur das WLAN wird an- und abgeschaltet.

### Wiederverbinden

```
Verbindung zum externen WLAN verloren
  ├─ bis zu 3 Wiederholungen (der WLAN-Stack verbindet selbst neu)
  ├─ danach: Fallback-WLAN, falls eingetragen
  ├─ sonst: syncFailed() → TSF-Fallback rechnet → Heimnetz aus (Hotspot bleibt)
  └─ nächster Versuch nach syncRetryMin (checkWifiStart), egal ob der Hotspot läuft
```

Jede Trennung steht mit dem Grund des WLAN-Treibers im Protokoll (`STA disconnected: reason N`, z. B.
2 = Anmeldung abgelaufen, 8 = selbst getrennt, 15/204 = Passwort falsch?, 201 = Netz nicht gefunden);
die Weboberfläche zeigt ihn im Klartext (`logTranslations.js`).

### Captive Portal

Der DNS-Server beantwortet jede Anfrage mit `4.3.2.1`. Die Erkennungs-URLs der Betriebssysteme werden in
`captive_portal.cpp` abgefangen (Android `/generate_204`, Apple `/hotspot-detect.html`, Windows
`/connecttest.txt` und `/ncsi.txt`, Firefox `/canonical.html`, Windows 10 `/wpad.dat` → 404).
Unbekannte URLs leiten auf die Startseite um.

---

## Uhrzeit-Quellen

| Quelle | Wann | Genauigkeit |
|---|---|---|
| **NTP** (`pool.ntp.org`, `time.nist.gov`) | sobald das externe WLAN eine IP vergibt | Millisekunden |
| **Gespeicherte Zeit** (NVS) | beim Start, wenn die Uhr noch leer ist | Stand des letzten Speicherns |
| **TSF-Fallback** | WLAN läuft, aber kein NTP | wenige Sekunden |
| **RDS** (optional) | kein WLAN, Schalter aktiv, Modul bestückt | Minutenauflösung |

Zeitzone: `CET-1CEST,M3.5.0,M10.5.0/3` (MEZ/MESZ mit automatischer Umstellung).
Im Deep Sleep läuft die Uhr über den RTC-Takt weiter (siehe [hardware.md](hardware.md)).

### NTP

1. IP erhalten → `syncTimeWithNTP()` merkt die aktuelle Uhrzeit und startet SNTP neu.
2. Sync fertig → Abweichung wird geloggt (`NTP sync: Abweichung +125ms`), Status „synchronisiert“,
   Zeit in NVS gesichert, TSF-Snapshot ausgelöst.
3. Während der Sync gültig ist, wird die Zeit alle 30 min in NVS gesichert.
4. Liegt der letzte NTP-Sync über 24 h zurück, gilt er als veraltet (`NTP veraltet`).

Hinweis: `sntp_get_sync_status()` meldet „fertig“ nur einmal und setzt sich danach zurück. Der Status
wird deshalb im Sync-Callback gesetzt, nicht gepollt.

### Systemwarnungen

Ist **LEDs → LED-Warnungen aktiv** eingeschaltet und die Lampe an (Helligkeit > 0), zeigt sie:
- **rot**: keine gültige Uhrzeit (vor 2020),
- **gelb**: letzte erfolgreiche Zeitquelle (NTP, TSF oder RDS) über 24 h her bzw. seit dem Start
  nach 30 min noch keine.

Eine ausgeschaltete Lampe wird durch Warnungen nie eingeschaltet.

---

## TSF-Fallback (`src/tsf_clock.cpp`)

Uhr stellen ohne Internet aus den Beacon-Zählern (TSF) der WLANs in der Nähe. **Ausführlich mit
durchgerechnetem Beispiel über mehrere Tage: [tsf-uhrzeit.md](tsf-uhrzeit.md).** Kurzfassung:

```
Snapshot (Zeit bekannt):  unix_ref, tsf_ref, Gang   pro AP
Fallback (Zeit unbekannt): unix_jetzt = unix_ref + (tsf_jetzt − tsf_ref) · (1 − Gang)
```

- **Bei jedem NTP-Versuch:** Erfolg → Snapshot (Genauigkeitstest, Gang messen, Bezüge erneuern);
  Fehlschlag → Fallback (Uhrzeit = Median der AP-Schätzungen, Ausreißer > 30 s raus, mindestens 3 APs;
  neu gestartete APs und Ausreißer werden neu basiert). Je 2 s Lauschen auf dem aktuellen Kanal.
- **Gangkorrektur:** Gang je AP gemessen zwischen zwei NTP-Snapshots mit ≥ 2 h Abstand, gemittelt.
- **Generation** je Bezug (0 = NTP, n = abgeleitet) nur zur Information; gerechnet wird mit allen.
- **Snapshot von Hand** nur bis 10 min nach einem NTP-Abgleich in der aktuellen Wachphase
  (`isTsfSnapshotAllowed()` über `msSinceNtpThisBoot()`, sonst 409); nach Deep Sleep ist die Systemzeit über
  den RC-Takt gelaufen.
- Liste mit bis zu 16 APs im NVS (Schlüssel `tsfData4`), je AP mit den letzten 4 Gangmessungen.
- Erfolg zählt als gültige Zeitquelle für die Systemwarnung (nicht als NTP).
- Schalter: **TSF-Fallback** (Standard an).

## RDS-Uhrzeit (`src/rds_clock.cpp`, optional)

UKW-Sender übertragen in RDS-Gruppe 4A Datum und Uhrzeit in **UTC** (plus Ortszeit-Offset, der nur
informativ ist). Empfang über ein RDA5807M-Modul (I²C-Adresse 0x11, Pins in [hardware.md](hardware.md)).

```
OFF        → Modul einschalten
POWERING   → 300 ms warten, Chip initialisieren (antwortet er nicht → kein Signal)
TUNING     → je Kanal 500 ms, RSSI ≥ 25 → LOCKED, sonst nächster Kanal (87,5–108 MHz, 100 kHz)
LOCKED     → alle 100 ms RDS lesen; Zeit wird erst gesetzt, wenn zwei fehlerfreie CT-Pakete
             50–70 s auseinander liegen und zur tatsächlich vergangenen Zeit passen (±3 s)
DONE       → Modul aus; nach 24 h erneuter Abgleich
NO_SIGNAL  → Modul aus; nach 1 h neuer Versuch
```

- Beim Einschalten (nicht nach Deep Sleep) wird das Modul kurz eingeschaltet und per I²C angefragt.
  Antwortet es nicht, läuft RDS nicht und der Schalter in der Weboberfläche ist gesperrt.
- Läuft nur ohne NTP und ohne externe WLAN-Verbindung; pausiert im APSTA-Modus.
- Der zuletzt gute Sender wird gespeichert und beim nächsten Mal zuerst versucht.
- Schalter: **RDS-Uhrzeit** (Standard aus).
