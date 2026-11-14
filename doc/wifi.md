# LisasLamp ESP32 — WiFi-Dokumentation

_Stand: November 2026_

---

## Überblick

Der WiFi-Controller (`src/wifi_controller.cpp`) verwaltet alle Netzwerkfunktionen: eigener Hotspot, externe WLAN-Verbindung, NTP-Zeitsynchronisation und die HTTP-API für die Web-UI.

---

## Die drei Betriebsmodi

| Modus | Wann | Beschreibung |
|---|---|---|
| **APSTA** | Manuell (langer Tastendruck) oder Systemeinstellungsänderung | Hotspot sichtbar **und** mit externem WLAN verbunden; Web-UI erreichbar |
| **STA-only** | Automatischer NTP-Sync alle 5 Stunden | Stille Verbindung zum externen WLAN; kein sichtbarer Hotspot |
| **AP-only** | Kein externes WLAN konfiguriert | Nur eigener Hotspot; keine NTP-Synchronisation möglich |

---

## Automatischer Start/Stop

### Automatischer Start (alle 5h)
`checkWifiStart()` prüft in `wifiLoop()` ob `START_WIFI_AFTER_MS` (5 Stunden) seit dem letzten aktiven WiFi vergangen sind. Wenn ja, startet es WiFi im STA-only-Modus für einen stillen NTP-Sync.

### Automatischer Stop (60s ohne Client)
`checkWifiStop()` stoppt WiFi wenn 60 Sekunden lang kein Gerät mit dem Hotspot verbunden ist (`STOP_WIFI_AFTER_MS`). Solange ein Client verbunden ist, bleibt WiFi aktiv.

**Hinweis:** Der Webserver (`AsyncWebServer`) wird nur einmal gestartet und läuft danach dauerhaft. Nur die WiFi-Verbindung selbst wird gestartet/gestoppt — ein erneutes `server.begin()` würde zu Instabilität führen.

---

## Reconnect-Logik

Wenn die STA-Verbindung abbricht, versucht der Event-Handler `WiFiEvent()` automatisch, die Verbindung wiederherzustellen:

```
Verbindungsabbruch
  ├─ Retry 1–3 (sofort, im selben Event-Handler)
  ├─ Nach 3 Fehlversuchen:
  │   ├─ Fallback-WLAN konfiguriert → auf Fallback-WLAN wechseln
  │   └─ Kein Fallback:
  │       ├─ APSTA-Modus → AP weiter lassen, STA aufgeben (WIFI_MODE_AP)
  │       └─ STA-only → alles stoppen (WIFI_MODE_NULL)
  └─ Alle 5 Minuten: tryReconnectSta() versucht erneuten Aufbau
      ├─ Wechselt abwechselnd zwischen Primär- und Fallback-WLAN
      └─ Stellt APSTA-Modus wieder her wenn AP aktiv war
```

`tryReconnectSta()` läuft in `wifiLoop()` und prüft alle 5 Minuten ob eine neue Verbindung möglich ist.

---

## NTP-Zeitsynchronisation

### Ablauf
1. Bei `ARDUINO_EVENT_WIFI_STA_GOT_IP` wird `syncTimeWithNTP()` sofort aufgerufen.
2. `syncTimeWithNTP()` speichert die aktuelle RTC-Zeit und ruft `sntp_restart()` auf.
3. Nach erfolgreicher Synchronisation wird `ntpSyncCallback()` aufgerufen.

### Drift-Logging
Der Callback berechnet die Abweichung zwischen alter RTC-Zeit und der neuen NTP-Zeit:
```c
driftMs = (newTime - preSyncTime) in ms
```
Dieser Wert wird im Eventlog gespeichert (z.B. `"NTP sync: Abweichung +1240ms"`).

Der ESP32-C3 Super Mini hat keinen externen 32.768-kHz-Quarz — der interne RC-Oszillator driftet 1–2% (ca. 864–1728 Sekunden/Tag). Das Drift-Logging macht diesen Fehler sichtbar.

### NVS-Backup
`updateTelemetryWiFiStatus()` läuft jede Minute und speichert den aktuellen Unix-Timestamp in NVS (`saveLastKnownTime()`). Nach einem Neustart ohne Netzwerk stellt `main.cpp` diese Zeit wieder her — besser als bei Epoch 0 (1. Januar 1970) zu starten.

### Zeitzone
Konfiguriert in `initWiFiController()`:
```c
configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
```
Mitteleuropäische Zeit (UTC+1) mit automatischer Sommerzeit (UTC+2).

---

## Captive Portal

Wenn sich ein Gerät mit dem Hotspot verbindet, erkennen die meisten Betriebssysteme automatisch, dass kein Internet vorhanden ist, und öffnen einen "Anmelden"-Dialog. Der DNS-Server antwortet auf alle Anfragen mit der eigenen IP (`4.3.2.1`), die HTTP-Anfragen der Captive-Portal-Erkennung werden mit Weiterleitungen beantwortet.

Die IP `4.3.2.1` (statt des üblichen `192.168.4.1`) ist erforderlich, weil Samsung-Geräte private IP-Adressen nicht als Captive-Portal-Ziel akzeptieren.

Spezialregeln im Code (`captive_portal.cpp`):
- iOS/macOS: Antworttext darf das Wort "Success" nicht enthalten
- Windows 10: `/wpad.dat` muss 404 zurückgeben (sonst Panikschleife)
- Android: `/generate_204` und `/gen_204` weiterleiten
- Firefox: `/canonical.html` und `/success.txt` behandeln

---

## Konfiguration

Relevante Konstanten in `wifi_controller.h`:

| Konstante | Wert | Bedeutung |
|---|---|---|
| `STOP_WIFI_AFTER_MS` | 60.000 ms | 60s ohne Client → Auto-Stop |
| `START_WIFI_AFTER_MS` | 18.000.000 ms | 5h → Auto-Start für NTP-Sync |
| `WIFI_CHANNEL` | 6 | Kanal des eigenen Hotspots |
| `MAX_CLIENTS` | 4 | Maximale Anzahl gleichzeitiger Clients |
| `localIP` | `4.3.2.1` | Feste IP des Hotspots |
