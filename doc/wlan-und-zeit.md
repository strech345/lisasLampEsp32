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
| **STA** | Einschalten/Aufwachen und automatischer Zeitabgleich alle 5 h (nur mit eingetragenem Heim-WLAN) | nein |
| **APSTA** | langer Tastendruck, während Heim-WLAN eingetragen ist (läuft STA schon, kommt der AP dazu) | ja |
| **AP** | langer Tastendruck ohne eingetragenes Heim-WLAN | ja |

Der Hotspot startet **nur** per langem Tastendruck (bzw. nach dem Speichern von WLAN-Einstellungen, weil
man dann ohnehin verbunden ist). Ohne Heim-WLAN bleibt das WLAN nach dem Start aus.

| Einstellung | Wert |
|---|---|
| Hotspot-IP | `4.3.2.1` (öffentlicher Bereich, Samsung-Geräte akzeptieren sonst das Captive Portal nicht) |
| Max. Geräte im Hotspot | 4 |
| Sendeleistung / Bandbreite | 17 dBm / HT40, WLAN-Schlafmodus aus (aus Messungen ermittelt) |
| Kanal | beim Einschalten der beste aus 1/6/11 per Scan; im RTC-Speicher gemerkt → nach Deep Sleep kein Scan |
| Standard-Zugangsdaten | aus `include/secrets.h` (siehe [README](../README.md)) |

### Automatischer Start und Stopp

- **Stopp:** 1 min kein Gerät im Hotspot → WLAN komplett aus (`STOP_WIFI_AFTER_MS`).
- **Start:** 5 h nach dem letzten aktiven WLAN → STA-only für den NTP-Abgleich (`START_WIFI_AFTER_MS`).
- Der Webserver wird nur einmal gestartet und läuft weiter; nur das WLAN wird an- und abgeschaltet.

### Wiederverbinden

```
Verbindung zum externen WLAN verloren
  ├─ bis zu 3 Wiederholungen (der WLAN-Stack verbindet selbst neu)
  ├─ danach: Fallback-WLAN, falls eingetragen
  ├─ sonst: APSTA → nur noch AP; STA-only → WLAN aus
  └─ alle 5 min: neuer Versuch (abwechselnd Haupt- und Fallback-WLAN)
```

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

Ist **Warnungen** aktiviert und die Lampe an (Helligkeit > 0), zeigt sie:
- **rot**: keine gültige Uhrzeit (vor 2020),
- **gelb**: letzte erfolgreiche Zeitquelle (NTP, TSF oder RDS) über 24 h her bzw. seit dem Start
  nach 30 min noch keine.

Eine ausgeschaltete Lampe wird durch Warnungen nie eingeschaltet.

---

## TSF-Fallback (`src/tsf_clock.cpp`)

Jeder Access Point sendet ca. alle 100 ms ein Beacon mit einem 64-Bit-Zähler (TSF, Mikrosekunden seit
AP-Start). Der Zähler läuft unabhängig vom Internet weiter:

```
Snapshot (Zeit bekannt):  unix_ref, tsf_ref   pro AP
Fallback (Zeit unbekannt): unix_jetzt = unix_ref + (tsf_jetzt − tsf_ref)
```

- **Snapshot:** nach jedem NTP-Sync (oder manuell in der Weboberfläche) 2 s Promiscuous-Scan.
  Bis zu 16 APs werden im NVS gehalten; neue APs verdrängen den schwächsten Eintrag
  (Score = Signalstärke + Laufzeit des APs, stabile APs bevorzugt).
- **Fallback:** nur im APSTA-Modus ohne NTP, einmal pro Start (geprüft im Minutentakt). Für jeden
  bekannten AP eine Schätzung, Ausreißer > 30 s vom Median verworfen, mindestens 3 APs nötig,
  Median wird Systemzeit. APs mit zurückgesetztem Zähler (Neustart) werden neu basiert.
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

- Läuft nur ohne NTP und ohne externe WLAN-Verbindung; pausiert im APSTA-Modus.
- Der zuletzt gute Sender wird gespeichert und beim nächsten Mal zuerst versucht.
- Schalter: **RDS-Uhrzeit** (Standard aus).
