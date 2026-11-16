# LisasLamp ESP32 — Architekturübersicht

_Stand: November 2026_

---

## Module

| Datei | Aufgabe |
|---|---|
| `src/main.cpp` | Einstiegspunkt: `setup()` und `loop()`, globale Zustandsvariablen, WiFi-Callback |
| `src/lamp_state.cpp` | Zustandsmaschine: bestimmt welcher LED-Effekt gilt; wertet alle Modi aus |
| `src/led.cpp` | WS2812FX-Treiber: Helligkeit, Farbe, Animationen, Blink-Effekte |
| `src/button.cpp` | Drehencoder-Auswertung: Klick, Langer Druck, Drehen → Callbacks |
| `src/alarm.cpp` | Alarmverwaltung: Zeitprüfung, Farb- und Helligkeitskurve beim Aufwachen |
| `src/good_night.cpp` | Einschlaf-Modus: Helligkeit dimmt über konfigurierte Dauer auf 0 |
| `src/celebration.cpp` | Feiertagserkennung: startet Effekt an konfigurierten Daten |
| `src/alerts.cpp` | Kurzzeit-Statusmeldungen (OK/Warning/Error) mit optionalem Timeout |
| `src/sleep_manager.cpp` | Deep-Sleep-Verwaltung: berechnet Aufwachzeit, leitet Schlaf ein |
| `src/wifi_controller.cpp` | WiFi-Steuerung: AP/STA/APSTA, Auto-Start/Stop, NTP-Sync, Reconnect |
| `src/captive_portal.cpp` | Webserver- und DNS-Setup; leitet alle unbekannten URLs zum Portal weiter |
| `src/route_handlers.cpp` | HTTP-Handler: REST-API für Konfiguration, Status, OTA |
| `src/store.cpp` | NVS-Persistenz: Konfiguration und Systemeinstellungen lesen/schreiben |
| `src/event_log.cpp` | Ringpuffer mit 200 Einträgen für interne Ereignisse; thread-safe |
| `src/json_utils.cpp` | JSON-Serialisierung/-Deserialisierung für alle Datenstrukturen |
| `src/rgb_effects.cpp` | Hilfsfunktionen für LED-Farbberechnungen (z.B. Sonnenaufgangs-Fade) |
| `src/system_utils.cpp` | Zeithelfer: `isTimeForAction()`, `getTimeoutState()` |
| `src/tsf_clock.cpp` | TSF-Uhrzeit-Fallback: Beacon-Scan per Promiscuous-Modus, Medianfilter |
| `src/rds_clock.cpp` | RDS-Uhrzeit-Fallback: RDA5807M FM-Chip, RDS-CT-Gruppe auswerten |

---

## Boot-Vorgang (`setup()`)

1. **Serial** — USB-CDC-Debug-Ausgabe starten (max. 5s warten)
2. **LittleFS** — Dateisystem für Web-UI-Assets mounten
3. **Konfiguration laden** — `loadFullConfig()` aus NVS; Standardwerte falls leer
4. **LED init** — WS2812FX starten, Status-LED einschalten
5. **Systemeinstellungen laden** — SSID, Passwörter, Flags aus NVS
6. **Module initialisieren** — `initLampState()`, `initSleepManager()`, `initButtonCallbacks()`, `initTsfClock()`, `initRdsClock()` mit Zeigern auf globale Variablen
7. **Farbmodus anwenden** — konfigurierte LED-Farbe setzen
8. **Alarme wiederherstellen** — `setAlarms()` berechnet nächste Auslösezeit
9. **WiFi und Webserver** — Routen initialisieren, Controller konfigurieren, `startWifi()`
10. **Aufwachgrund prüfen** — GPIO → Default-Modus; Timer → Sleep-Modus
11. **Helligkeit und Encoder** — Encoder mit gespeichertem Helligkeitswert starten
12. **Systemzeit wiederherstellen** — NVS-Timestamp laden wenn kein NTP vorhanden
13. **Boot-Ereignis** protokollieren

---

## Hauptschleife (`loop()`)

```
ledUpdate()           — WS2812FX-Animationen und Blink-Effekte
wifiLoop()            — DNS, Auto-Start/Stop, Telemetrie, Reconnect
tsfClockLoop()        — TSF-Beacon-Scan und Zeitberechnung
rdsClockLoop()        — RDS-FM-Scan und Zeitberechnung
rotary_loop()         — Encoder-Ereignisse auswerten und Callbacks auslösen
checkAlarmStates()    — Alarm auslösen wenn Zeit erreicht
checkAlertState()     — Zeitbegrenzten Alert beenden wenn abgelaufen
checkGoodNightMode()  — Gute-Nacht-Modus nach Ablauf der Dauer beenden
checkCelebrationLogic()— Feiertagsdatum prüfen, Modus starten/stoppen
checkLampState()      — Zustandsmaschine: aktiven Modus bestimmen (letzte Priorität)
updateLed()           — LED-Effekte bei Zustandswechsel anwenden
checkToSave()         — Verzögerten NVS-Schreibvorgang ausführen
checkToGoSleep()      — Deep Sleep einleiten wenn Bedingungen erfüllt
```

---

## Modul-Kommunikation

Die Module kommunizieren nicht direkt miteinander, sondern über Zeiger auf globale Variablen in `main.cpp`. Die wichtigsten sind:

| Variable | Typ | Wer schreibt | Wer liest |
|---|---|---|---|
| `lampState` | `LampState` | `lamp_state.cpp`, `button.cpp` | `lamp_state.cpp`, `sleep_manager.cpp` |
| `appConfig` | `FullConfig` | `main.cpp` (WiFi-Callback), `button.cpp` | alle Module |
| `systemSettings` | `SystemSettings` | `main.cpp` (WiFi-Callback) | `wifi_controller.cpp` |
| `wifiTracker` | `WiFiTestTracker` | `wifi_controller.cpp` | `lamp_state.cpp`, `route_handlers.cpp` |

Der Webserver ruft bei Konfigurationsänderungen `onStateUpdatedFromWifi()` in `main.cpp` auf, die den Zustand aktualisiert und betroffene Module benachrichtigt.
