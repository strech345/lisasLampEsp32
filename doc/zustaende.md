# LisasLamp ESP32 — Lampenzustände

_Stand: November 2026_

---

## Alle Zustände

| Zustand | Wert | Beschreibung | LED-Effekt |
|---|---|---|---|
| `LAMP_STATE_DEFAULT` | 0 | Normalbetrieb | Konfigurierte Farbe und Animation |
| `LAMP_STATE_ALARM` | 1 | Wecker läuft | Sonnenaufgangs-Farbverlauf, Helligkeit steigt linear an |
| `LAMP_STATE_GOOD_NIGHT` | 2 | Einschlaf-Modus | Aktuelle Farbe, Helligkeit dimmt linear auf 0 |
| `LAMP_STATE_ERROR` | 3 | Kurzzeit-Fehleranzeige (Alert) | Alle LEDs rot |
| `LAMP_STATE_SUCCESS` | 4 | Kurzzeit-Erfolgsmeldung (Alert) | Alle LEDs grün |
| `LAMP_STATE_WARNING` | 5 | Kurzzeit-Warnung (Alert) | Alle LEDs gelb |
| `LAMP_STATE_SLEEP` | 6 | Lampe aus | LEDs aus (Helligkeit 0) |
| `LAMP_STATE_CELEBRATION` | 7 | Feiertagsmodus | Bunter Animations-Effekt |
| `LAMP_STATE_SYS_ERROR` | 8 | Systemfehler (Uhrzeit fehlt) | Pixel 0 blinkt rot |
| `LAMP_STATE_SYS_WARNING` | 9 | Systemwarnung (NTP veraltet >24h) | Pixel 0 blinkt gelb |

---

## Prioritätsreihenfolge

`checkLampState()` in `lamp_state.cpp` prüft Bedingungen von höchster zu niedrigster Priorität:

```
1. Alert (Error/Warning/OK)    — sofortige Statusanzeige
2. Alarm                       — Wecker läuft
3. Gute Nacht                  — Einschlafen aktiv
4. Celebration                 — Feiertagsmodus
5. Sleep                       — Lampe aus (bleibt bis Nutzeraktion)
6. Systemwarnung               — NTP-Sync fehlt (nur wenn Warnungen aktiviert)
7. Default                     — Normalbetrieb
```

Sleep und Zustandsübergänge zu Sleep haben keine niedrigere Priorität als die Reihenfolge suggeriert — Sleep wird explizit als "bleibt bestehen" behandelt: der Zustand ändert sich erst durch Nutzeraktion.

---

## Übergangstabelle

| Ereignis | Neuer Zustand | Auslösende Datei |
|---|---|---|
| Boot via GPIO (Encoder-Druck) | Default | `main.cpp` |
| Boot via Timer (Deep Sleep) | Sleep | `main.cpp` |
| Alarm-Zeit erreicht | Alarm | `alarm.cpp` → `checkAlarmStates()` |
| Alarm-Dauer abgelaufen | Sleep | `lamp_state.cpp` → Alarm → Sleep |
| Gute-Nacht aktiviert | Good Night | `lamp_state.cpp` → `onShortPress()` |
| Gute-Nacht-Dauer abgelaufen | Sleep | `good_night.cpp` → `checkGoodNightMode()` |
| Feiertagsdatum erkannt | Celebration | `celebration.cpp` → `checkCelebrationLogic()` |
| `startAlert(ALERT_OK, ...)` | Success | `alerts.cpp` |
| `startAlert(ALERT_WARNING, ...)` | Warning | `alerts.cpp` |
| `startAlert(ALERT_ERROR, ...)` | Error | `alerts.cpp` |
| Alert-Dauer abgelaufen | vorheriger Zustand | `alerts.cpp` → `checkAlertState()` |
| Warnungen aktiviert + NTP >24h alt | SysWarning | `lamp_state.cpp` |
| Warnungen aktiviert + Zeit unbekannt | SysError | `lamp_state.cpp` |

---

## Welche Tastengeste beendet welchen Modus

| Aktiver Modus | Geste | Wirkung |
|---|---|---|
| Sleep | Kurzer Druck | → Default (grüner Flash) |
| Alarm | Kurzer Druck | → Default, Alarm stoppen (roter Flash) |
| Gute Nacht | Kurzer Druck | → Default, Modus stoppen (roter Flash) |
| Celebration | Kurzer Druck | → Default, Modus stoppen (roter Flash) |
| Alert (Error/Warning/OK) | Kurzer Druck | Alert beenden, → letzter normaler Zustand |
| Default | Kurzer Druck | → Gute-Nacht-Modus starten (grüner Flash) |
| Beliebig | Langer Druck | WiFi starten (APSTA-Modus) |
| Beliebig | Drehen | Helligkeitsstufe ändern (1–10) |

---

## Systemwarnungen

Systemwarnungen werden nur angezeigt wenn `systemSettings.warningsEnabled = true` (Standard: ein).
Sie zeigen sich als einzeln blinkender Pixel (Pixel 0 = `WARNING_PIXEL`) damit sie von normalen Effekten unterscheidbar sind.

- **SysError** (rot blinken): `time()` liefert einen Wert vor 2020 — NTP hat noch nie synchronisiert.
- **SysWarning** (gelb blinken): Letzter erfolgreicher NTP-Sync liegt mehr als 24h zurück.

Beide werden durch einen erfolgreichen NTP-Sync automatisch aufgelöst.
