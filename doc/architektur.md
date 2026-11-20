# Architektur

_Stand: November 2026_

---

## Module

| Datei | Aufgabe |
|---|---|
| `src/main.cpp` | `setup()`/`loop()` und globaler Zustand — nur Reihenfolge, keine Logik |
| `src/web_commands.cpp` | Auftragsablage Web → `loop()`: `queueWebCommand()`, `processWebCommands()` |
| `src/lamp_state.cpp` | Zustandsmaschine: aktiven Modus bestimmen, LEDs beim Zustandswechsel setzen |
| `src/led.cpp` | WS2812FX-Treiber: Farbe, Helligkeitsstufen 0–7, Animation, Blitz, Status-LED |
| `src/button.cpp` | Drehknopf und Boot-Taste: rastungsgenaues Drehen, kurzer/langer Druck |
| `src/alarm.cpp` | Wecker: Auslösung (10-min-Fenster), Helligkeitsverlauf, Zeit bis zum nächsten Wecker |
| `src/good_night.cpp` | Gute-Nacht-Modus: Helligkeit dimmt über die eingestellte Dauer auf 0 |
| `src/celebration.cpp` | Feier-Modus an festen Tagen ab 7:00 |
| `src/alerts.cpp` | Kurze Hinweise (OK/Warnung/Fehler) mit Ablaufzeit |
| `src/sleep_manager.cpp` | Deep Sleep: Bedingungen, Aufwachzeit inkl. Uhrabweichung, Wakeup-Pins |
| `src/wifi_controller.cpp` | WLAN AP/STA/APSTA, Auto-Start/-Stopp, Kanalwahl, NTP, Wiederverbinden |
| `src/captive_portal.cpp` | Webserver-/DNS-Setup, Captive-Portal-URLs, POST-Prüfung (JSON, Origin, Größe) |
| `src/route_handlers.cpp` | HTTP-API (siehe unten) |
| `src/json_utils.cpp` | JSON ↔ Strukturen inkl. Wertebereichsprüfung |
| `src/store.cpp` | NVS-Persistenz, Standardwerte (aus `secrets.h`), verzögertes Speichern |
| `src/event_log.cpp` | Ringpuffer (200 Einträge) für das Ereignisprotokoll, Mutex-geschützt |
| `src/tsf_clock.cpp` | TSF-Uhrzeit-Fallback über WLAN-Beacons |
| `src/rds_clock.cpp` | RDS-Uhrzeit über RDA5807M (optional) |
| `src/rtc_crystal.cpp` | optionaler 32-kHz-Uhrenquarz |
| `src/system_utils.cpp`, `src/debug_utils.cpp` | `isTimeForAction()`, `serialPrint()` |
| `include/types.h` | gemeinsame Strukturen (`FullConfig`, `SystemSettings`, `LampState`, …) |
| `include/pins.h` | alle GPIOs (siehe [hardware.md](hardware.md)) |
| `include/secrets.example.h` | Vorlage für Standard-Zugangsdaten (`secrets.h` ist gitignored) |

Weitere Doku: [zustaende.md](zustaende.md), [bedienung.md](bedienung.md),
[wlan-und-zeit.md](wlan-und-zeit.md), [build-und-deploy.md](build-und-deploy.md).

---

## Tasks und Thread-Sicherheit

Diese Stellen laufen außerhalb von `loop()`:

| Kontext | Was dort passiert | Regel |
|---|---|---|
| `async_tcp`-Task (Webserver) | HTTP-Handler | nur prüfen, Auftrag ablegen, antworten |
| WiFi-Event-Task | `WiFiEvent()` | nur Nachricht in eine Queue (`postMsg`) |
| lwIP-Task | SNTP-Callback | nur Drift berechnen, Nachricht in die Queue |
| WiFi-Task (Promiscuous) | Beacon-Callback (TSF) | nur in Puffer schreiben, Speicherbarriere vor dem Zähler |

**Auftragsablage (`web_commands.cpp`):** Ein Handler ruft `queueWebCommand(type, data)` auf. Das kopiert
die Daten unter einem Mutex in `s_pending` (pro Auftragstyp ein Platz, der letzte gewinnt).
`processWebCommands()` läuft als Erstes in `loop()` und führt alles aus: Config übernehmen,
speichern, WLAN neu starten (nur wenn SSID/Passwort geändert), Neustart/Werksreset (ca. 500 ms
verzögert, damit die HTTP-Antwort noch rausgeht). LEDs, WLAN, NVS und `ESP.restart()` werden damit nur
aus `loop()` angefasst.

---

## Boot (`setup()`)

1. `debugInit()` (Serial über UART0), LittleFS, `loadFullConfigOrDefault()`,
   `loadSystemSettingsOrDefault()` (enthält die LED-Anzahl)
2. **Aufwachgrund** (`applyWakeupReason()`): Taster → Normalbetrieb mit Helligkeit 1; Timer → Sleep
3. **LEDs sofort** setzen (Anzahl, Farbe, Helligkeit; bei Timer-Wakeup aus) — vor allem Langsamen
4. `initClock()`: Zeitzone setzen, Uhrzeit aus NVS falls leer — vor den Weckern
5. RTC-Quarz prüfen (Taktquelle nur beim Boot umschaltbar)
6. Module initialisieren (Zeiger auf den globalen Zustand), Wecker übernehmen
7. Auftragsablage, Routen, WLAN-Controller (Kanal aus RTC-Speicher oder Scan), `startWifi(false)` —
   nur Heim-WLAN, kein Hotspot (der kommt nur per langem Tastendruck)
8. Drehknopf (Ruhepegel lernen)

Gemessen: Licht nach ca. 60 ms, einsatzbereit nach ca. 0,35 s (Deep-Sleep-Wakeup) bzw. 3,5 s
(Einschalten mit Kanal-Scan).

## Hauptschleife (`loop()`)

```
processWebCommands()       Aufträge vom Webserver ausführen
wifiLoop()                 Events, DNS, Auto-Start/-Stopp, NTP-Status, Wiederverbinden
tsfClockLoop()             TSF-Snapshot/-Fallback
rdsClockLoop()             RDS-Zustandsmaschine
statusLedTick()            Blinkmuster der Status-LED
rotary_loop()              Drehknopf und Tasten
ledUpdate()                WS2812FX rendern, Blitz beenden
checkAlarmStates()         Wecker starten/stoppen (alle 10 s)
checkAlertState()          Hinweis-Ablauf
checkGoodNightMode()       Gute-Nacht-Ablauf
checkCelebrationLogic()    Feiertag prüfen
checkLampState()           aktiven Zustand bestimmen (muss nach den Modi laufen)
updateLed()                LEDs beim Zustandswechsel setzen, Helligkeitsverläufe alle 5 s
checkToSave()              verzögertes Speichern (5 s)
checkToGoSleep()           Deep Sleep prüfen (alle 10 s)
```

---

## Persistenz (NVS-Namespace `lisas_lamp_cfg`)

| Schlüssel | Inhalt |
|---|---|
| `fullConfig` | `FullConfig`: bis zu 6 Licht-Sets + aktives Set, Helligkeit, 10 Wecker, Dauern |
| `systemSettings` | `SystemSettings`: Hotspot, 2 externe WLANs, Schalter, RDS-Kanal, Quarz, LED-Anzahl |
| `tsfData` | bis zu 16 TSF-Referenzen |
| `lastKnownTime` | letzte gültige Uhrzeit (alle 30 min bei gültigem NTP) |

Strukturen werden binär gespeichert. **Ändert sich ihre Größe** (Firmware-Update), werden sie auf
Standard zurückgesetzt — siehe TODO in der [README](../README.md).

---

## HTTP-API

Alle POSTs: `Content-Type: application/json` (sonst 415), passender `Origin` falls gesendet (sonst 403),
Body höchstens 4096 Bytes (sonst 413). Fehler: `{"status":"error","message":"…"}`.
POSTs ohne Nutzdaten senden `{}`.

| Endpunkt | Methode | Inhalt |
|---|---|---|
| `/`, `/app.js`, `/pico.min.css`, `/meerkat.avif` | GET | Weboberfläche (gzip aus LittleFS) |
| `/ping` | GET | `pong` (Verbindungsprüfung der UI) |
| `/get_config` | GET | `FullConfig` als JSON |
| `/set_config` | POST | Teil-Update der `FullConfig`; fehlende Felder bleiben, `brightnessMode` wird ignoriert (Drehknopf ist Master), Wertebereiche geprüft |
| `/get_system_config` | GET | Hotspot/WLAN (Passwörter maskiert `******`), alle Schalter und `ledCount` |
| `/set_system_config` | POST | Hotspot/WLAN; SSID 1–32, Passwort leer (= unverändert) oder 8–63; `clearInternalPW`/`clearExternalPW`/`clearExternalPW2` entfernen ein Passwort |
| `/set_led_count` | POST | `{"count": 1–16}` → Anzahl der LEDs, sofort wirksam (in `SystemSettings`) |
| `/set_option` | POST | `{"option": "warnings\|deepSleep\|tsfFallback\|rdsClock\|rtcCrystal", "enabled": bool}`; `rtcCrystal` startet neu |
| `/get_status` | GET | Uhrzeit, NTP-Status, externe WLAN-Verbindung |
| `/get_event_log` | GET | Ereignisprotokoll (neueste zuerst) |
| `/get_tsf_fallback`, `/get_rds_clock`, `/get_rtc_crystal` | GET | Detailstatus der Zeitquellen |
| `/trigger_ntp_sync`, `/trigger_tsf_snapshot` | POST | Aktion auslösen |
| `/restart`, `/factory_reset` | POST | Neustart bzw. Werksreset (verzögert) |

Der Webserver ist ohne Login erreichbar; Schutz bietet das Hotspot-Passwort.
