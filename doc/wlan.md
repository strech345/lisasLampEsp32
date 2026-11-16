# LisasLamp ESP32 — WLAN-Konzept

_Stand: November 2026_

---

Detaillierte Dokumentation der WLAN-Funktionen: [wifi.md](wifi.md)

---

## Kurzübersicht: Betriebsmodi

| Modus | Wann | Hotspot sichtbar |
|---|---|---|
| **APSTA** | Langer Tastendruck oder Konfigurationsänderung | Ja — Web-UI erreichbar |
| **STA-only** | Automatischer NTP-Sync alle 5h | Nein |
| **AP-only** | Kein externes WLAN konfiguriert | Ja |

Die feste Hotspot-IP ist `4.3.2.1` (öffentlicher Adressbereich, nötig für Samsung-Geräte).

---

## NTP-Sync-Ablauf

```
STA verbunden → IP erhalten
  → syncTimeWithNTP()
     → RTC-Zeit sichern (für Drift-Berechnung)
     → sntp_restart()
        → ntpSyncCallback()
           → Drift loggen
           → onNtpSynced() → TSF-Snapshot auslösen
           → Zeit in NVS sichern (saveLastKnownTime)
```

Zeitzone: `CET-1CEST,M3.5.0,M10.5.0/3` (MEZ/MESZ, automatische Umschaltung).

---

## Fallback-Mechanismen

Wenn kein NTP verfügbar ist (kein WLAN oder Verbindung gescheitert):

1. **Gespeicherte Zeit** (NVS) — wird beim Boot sofort geladen
2. **TSF-Fallback** — WLAN-Beacons per Promiscuous-Scan (siehe [uhrzeit-fallback.md](uhrzeit-fallback.md))
3. **RDS-Fallback** — FM-Radio RDS-CT-Pakete (optional, siehe [uhrzeit-fallback.md](uhrzeit-fallback.md))

---

## Reconnect

Bei Verbindungsabbruch: bis zu 3 direkte Wiederholversuche, dann Wechsel zum Fallback-WLAN
(falls konfiguriert). Alle 5 Minuten erneuter Verbindungsversuch via `tryReconnectSta()`.

---

## Captive Portal

DNS-Server antwortet auf alle Anfragen mit `4.3.2.1`. Betriebssystem-spezifische
Erkennungs-URLs werden in `captive_portal.cpp` abgefangen (iOS, Android, Windows, Firefox).
