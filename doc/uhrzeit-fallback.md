# LisasLamp ESP32 — Uhrzeit-Fallback

_Stand: November 2026_

---

## Überblick

Der ESP32-C3 hat keine eigene Echtzeituhr. Normalerweise wird die Zeit per NTP synchronisiert.
Wenn kein WLAN verfügbar ist, gibt es zwei Fallback-Mechanismen:

| Fallback | Modul | Methode |
|---|---|---|
| **TSF-Fallback** | `src/tsf_clock.cpp` | WLAN-Beacons per Promiscuous-Scan |
| **RDS-Fallback** | `src/rds_clock.cpp` | FM-Radio RDS-CT-Pakete via RDA5807M |

Beide sind unabhängig voneinander und über Flags in `SystemSettings` einzeln aktivierbar:
- `tsfFallbackEnabled` — TSF-Fallback (Standard: ein)
- `rdsClockEnabled` — RDS-Fallback (Standard: aus)

---

## TSF-Fallback

### Grundprinzip

Jeder WLAN-Access-Point sendet alle 100 ms einen **Beacon-Frame**. Dieser enthält einen
64-Bit-TSF-Zähler (Timing Synchronization Function), der die Zeit in Mikrosekunden seit dem
ersten Beacon des APs misst. Der Zähler läuft monoton weiter, unabhängig vom Internet.

Der Algorithmus nutzt die Tatsache, dass der TSF-Zähler deterministisch läuft:

```
Snapshot-Zeitpunkt (NTP bekannt):
  unix_ref  = aktuelle Unix-Zeit [µs]
  tsf_ref   = TSF-Zähler des APs

Fallback-Zeitpunkt (NTP nicht verfügbar):
  tsf_now   = TSF-Zähler des APs (neu gemessen)
  unix_est  = unix_ref + (tsf_now - tsf_ref)
```

Je mehr APs sichtbar sind, desto mehr unabhängige Schätzungen gibt es. Die werden
dann gefiltert und der Median gewählt.

### Ablauf

1. **Snapshot** (nach NTP-Sync): Promiscuous-Modus für 2 Sekunden, alle Beacons erfassen.
   Pro AP (BSSID) wird genau ein Eintrag gespeichert. Bis zu `MAX_TSF_ENTRIES` (10) APs
   werden nach RSSI sortiert und im Flash (NVS) gespeichert.

2. **Fallback** (kein NTP, WiFi-Verbindung gescheitert): Neuer Promiscuous-Scan für 2 Sekunden.
   Für jeden AP der bereits im Snapshot vorhanden ist, wird eine Zeitschätzung berechnet.

3. **Filterung**: Ausreißer werden anhand von Standardabweichung und absolutem Grenzwert
   (30 Sekunden) entfernt. Danach muss die gefilterte Menge noch mindestens 3 APs enthalten.

4. **Medianwert**: Die verbleibenden Schätzungen werden sortiert. Der mittlere Wert (Median)
   wird als neue Systemzeit gesetzt.

5. **Rebasing**: Wenn ein TSF-Zähler kleiner als der gespeicherte Wert ist (Überlauf oder
   AP-Neustart), wird der Snapshot-Eintrag mit dem neuen TSF und der berechneten Medianzeit
   aktualisiert.

### Wann aktiv

- Der Fallback-Scan startet automatisch 60 Sekunden nach dem letzten fehlgeschlagenen NTP-Sync,
  wenn mindestens ein gespeicherter Snapshot vorhanden ist.
- Er startet nur wenn WiFi im APSTA-Modus läuft (STA aktiv, aber IP nicht erhalten).
- Nach einem erfolgreichen Fallback wird kein weiterer Versuch gestartet (bis zum nächsten Boot).

### Genauigkeit

Abhängig von der Anzahl sichtbarer APs und dem RTC-Drift zwischen Snapshot und Fallback.
Typisch sind Abweichungen von wenigen Sekunden. Die durchschnittliche Abweichung wird
nach der Berechnung im Eventlog protokolliert (`TSF: Zeit gesetzt N APs +-Xms`).

---

## RDS-Fallback

### Grundprinzip

FM-Radiosender übertragen neben Audio auch **RDS (Radio Data System)** Datenpakete.
Die RDS-Gruppe **Type 4A (Clocktime and Date)** enthält Zeit und Datum nach UTC mit Offset.
Das **RDA5807M**-FM-Modul empfängt diese Pakete per I2C.

### RDS-Group-4A-Format

```
Block B [Bits 15:12] = 0b0100 (Group Type 4A)
Block B [Bit 11]     = 0 (Version A)
Block B [Bits 1:0] + Block C [Bits 15:1] = MJD (Modified Julian Date)
Block C [Bit 0] + Block D [Bits 15:12]   = Stunden (0–23)
Block D [Bits 11:6]                      = Minuten (0–59)
Block D [Bits 5:0]                       = UTC-Offset (±30min-Schritte, Bit 5 = Vorzeichen)
```

MJD → Unix-Tag: `unix_day = MJD - 40587`

### Ablauf (Zustandsmaschine)

```
RDS_STATE_OFF
  → Modul einschalten (GPIO HIGH)
RDS_STATE_POWERING (300 ms warten)
  → Chip initialisieren, letzten bekannten Kanal oder 87.5 MHz
RDS_STATE_TUNING (500 ms pro Kanal)
  → RSSI prüfen: ≥ 25 → LOCKED, sonst nächster Kanal
  → Alle Kanäle durch: NO_SIGNAL
RDS_STATE_LOCKED (bis zu 35 Sekunden)
  → Alle 100 ms auf RDS-Group-4A prüfen
  → Zeit gefunden: Zeit setzen, Modul aus → DONE
  → Timeout: nächsten Kanal versuchen
RDS_STATE_DONE / RDS_STATE_NO_SIGNAL
  → Zustand bleibt bis zum nächsten Boot
```

### Wann aktiv

- Nur wenn `rdsClockEnabled = true` (Standard: aus, muss manuell aktiviert werden).
- Stoppt wenn WiFi eine STA-Verbindung aufbaut (`staConfigValid = true`) oder NTP synct (`clockSynced = true`).
- Läuft nicht wenn WiFi gerade im APSTA-Modus ist (um Interferenzen zu vermeiden).

### Hardware

| Signal | GPIO | Hinweis |
|---|---|---|
| Power | GPIO 6 | HIGH = Modul an |
| I2C SDA | GPIO 4 | RDA5807M-Adresse: 0x11 |
| I2C SCL | GPIO 5 | |

### Genauigkeit

RDS-CT hat eine Auflösung von 1 Minute. Die tatsächliche Abweichung hängt vom Sender ab —
öffentlich-rechtliche Sender synchronisieren ihre RDS-Zeit typisch auf UTC±1s.

---

## Zusammenspiel mit NTP

Die Priorisierung läuft über den `WiFiTestTracker`:

```
NTP-Sync verfügbar    → TSF-Snapshot anlegen; RDS-Modul bleibt aus
NTP-Sync verloren     → TSF-Fallback nach 60s; RDS-Fallback wenn aktiviert
TSF-Fallback gesetzt  → Systemzeit vorhanden; RDS bleibt aus
RDS-Zeit gesetzt      → Systemzeit vorhanden
```

Nach einem Neustart wird die zuletzt bekannte NTP-Zeit aus dem NVS geladen
(`loadLastKnownTime()`), um nicht bei Epoch 0 zu starten.
