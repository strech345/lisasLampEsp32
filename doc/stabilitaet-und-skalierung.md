# Stabilität und Skalierung — Befunde und Lösungen

_Stand: November 2026 · Grundlage: Arbeitsstand des Branches `cleanup2026` (inkl. nicht committeter Änderungen)_

Dieses Dokument listet Probleme und Verbesserungsmöglichkeiten, die der **Stabilität** (Lampe tut
zuverlässig das Richtige, auch nach Neustart, Stromausfall, Deep Sleep und bei mehreren Browsern) und der
**Skalierung** (mehr Funktionen, mehr Clients, mehr Lampen, mehr Code) dienen. Jeder Punkt nennt die
Stelle im Code, ein konkretes Fehlerszenario und einen Lösungsvorschlag.

**Priorität:** 🔴 hoch (sichtbares Fehlverhalten im Alltag) · 🟠 mittel (tritt unter bestimmten
Bedingungen auf oder bremst Erweiterungen) · 🟢 niedrig (Randfall, Aufräumen)

**Befund:** ✅ bestätigt (am Code nachvollzogen) · 🔍 plausibel (am Gerät prüfen)

**Aufwand:** S = bis 1 h · M = einige Stunden · L = Tag oder mehr

---

## Übersicht

| # | Thema | Prio | Befund | Aufwand |
|---|---|---|---|---|
| 1 | ~~Wecker löst nach Aufwachen/Neustart erneut aus~~ erledigt | 🔴 | ✅ | S |
| 2 | ~~Feier-Modus startet nach Aufwachen erneut (auch nachts)~~ erledigt | 🔴 | ✅ | S |
| 3 | ~~Nach Stromausfall: alte Uhrzeit und nächster Versuch erst nach 5 h~~ erledigt (ohne optionale Warnung) | 🔴 | ✅ | M |
| 4 | ~~`app.js` wird 1 Jahr gecacht → alte UI nach Update~~ erledigt | 🔴 | ✅ | S |
| 5 | Automatische Abfragen der UI verhindern das Hotspot-Abschalten | 🟠 | ✅ | S |
| 6 | Verlorene Änderungen bei schnellen `/set_config` + Lade-Rennen in der UI | 🟠 | ✅ | M |
| 7 | Webhandler lesen globalen Zustand ohne Sperre | 🟠 | ✅ | M |
| 8 | I²C- und WLAN-Treiberaufrufe aus dem Webserver-Task | 🟠 | ✅ | S |
| 9 | Kein Watchdog für `loop()` | 🟠 | ✅ | S |
| 10 | RDS-Uhr und Deep Sleep passen nicht zusammen | 🟠 | ✅ | M |
| 11 | Warnung „Zeit veraltet“ geht mit jedem Deep Sleep verloren | 🟠 | ✅ | S |
| 12 | Werksreset per BOOT-Taste schon nach 1 s | 🟠 | ✅ | S |
| 13 | Ereignisprotokoll als ein großer String (Heap-Spitze) | 🟠 | ✅ | M |
| 14 | Polling statt Push — skaliert schlecht mit mehreren Clients | 🟠 | ✅ | L |
| 15 | Kein OTA, UI und Firmware getrennt geflasht, keine Versionsprüfung | 🟠 | ✅ | M–L |
| 16 | Keine Absturzdiagnose über Neustarts hinweg | 🟠 | ✅ | M |
| 17 | Keine automatischen Tests / CI | 🟠 | ✅ | M–L |
| 18 | Aufwecken per Knopfdruck kann sofort „Gute Nacht“ auslösen | 🟢 | 🔍 | S |
| 19 | TSF-Erfassung bleibt an, wenn TSF währenddessen abgeschaltet wird | 🟢 | ✅ | S |
| 20 | `FullConfig` wird nach dem Laden nicht plausibilisiert | 🟢 | ✅ | S |
| 21 | WLAN-Nachrichten-Queue verwirft still | 🟢 | ✅ | S |
| 22 | Grenzwerte mehrfach im Code verteilt (Firmware, UI, Meldungen) | 🟢 | ✅ | M |
| 23 | Mehrere Lampen: gleicher Hotspot-Name, Personendaten zur Compile-Zeit | 🟢 | ✅ | S–M |
| 24 | Einmaliger Wecker akzeptiert ungültige Daten (31.02.) | 🟢 | ✅ | S |
| 25 | Mehrfache NVS-Schreibvorgänge pro Auftrag | 🟢 | ✅ | S |
| 26 | Logging baut Strings auch bei abgeschaltetem Log | 🟢 | ✅ | S |
| 27 | Nicht-statische globale Namen | 🟢 | ✅ | S |
| 28 | API ohne Schutz — auch im Heimnetz erreichbar | 🟠 | ✅ | S–M |

Empfohlene Reihenfolge am Ende: [Fahrplan](#fahrplan).

---

## A. Funktionale Fehler (Stabilität im Alltag)

### 1. 🔴 Wecker löst nach Aufwachen/Neustart erneut aus

**Stelle:** `src/alarm.cpp:35` (`last_triggered_day_for_alarm`), `src/alarm.cpp:126–137`

**Problem:** Welcher Wecker heute schon ausgelöst hat, steht nur im normalen RAM. Ein Aufwachen aus dem
Deep Sleep ist für den ESP32 ein Neustart — die Liste ist danach leer. Der Wecker hat ein Nachlauffenster
von 10 Minuten (`ALARM_GRACE_MINUTES`).

**Szenario:** Wecker 7:00 klingelt → kurzer Druck stoppt ihn um 7:02 → Lampe wird ausgedreht → Deep Sleep
(nächster Wecker erst nächste Woche) → um 7:06 wird der Knopf gedrückt (Aufwachen) → nach 10 s prüft
`checkAlarmStates()`: 7:00 liegt im 10-min-Fenster, „heute noch nicht ausgelöst“ → **der Wecker startet
erneut**. Dasselbe nach `ESP.restart()` (Einstellungen, Quarz-Umschaltung) oder einem Absturz.

**Lösung:** Auslösetag im RTC-Speicher halten (übersteht Deep Sleep und Software-Neustart, nur nicht
Stromausfall) und Jahr mit speichern, damit derselbe Jahrestag im Folgejahr nicht blockiert:

```cpp
// Jahr*1000 + Jahrestag des letzten Auslösens; -1 = nie (Initialwert gilt nur beim Kaltstart)
RTC_DATA_ATTR static int32_t s_lastTriggered[MAX_ALARMS] = {-1, -1, /* … */};
// Hinweis: RTC_DATA_ATTR mit Initialwert wird beim Kaltstart gesetzt; 0 wäre ein gültiger Tag (1. Jan.)
```

`setAlarms()` setzt den Eintrag weiterhin bei Änderungen zurück. Aufwand S.

### 2. 🔴 Feier-Modus startet nach Aufwachen erneut — auch nachts

**Stelle:** `src/celebration.cpp:15–16` (`celebrationActive`, `lastTriggeredDay`), Priorität in
`src/lamp_state.cpp:173–182`

**Problem:** Wie bei Punkt 1 liegt „heute schon gestartet/manuell beendet“ nur im RAM. Der Feier-Modus hat
Vorrang vor `LAMP_STATE_SLEEP`.

**Szenario:** Party-Tag, um 7:00 startet der Regenbogen, Nutzer beendet ihn per Druck und schaltet die
Lampe aus → Deep Sleep → **Timer-Wakeup um 23:00 für den Zeitabgleich** → `checkCelebrationLogic()` sieht
Party-Tag, Stunde ≥ 7, `lastTriggeredDay == -1` → **Regenbogen auf Stufe 7 mitten in der Nacht**, und die
Lampe geht nicht mehr in den Deep Sleep (Zustand ist nicht DEFAULT/SLEEP).

**Lösung:** `lastTriggeredDay` (mit Jahr) als `RTC_DATA_ATTR`. Zusätzlich sinnvoll: Feier-Modus nur
starten, wenn der Wechsel auf ≥ 7:00 **in dieser Wachphase beobachtet** wurde oder die Lampe nicht im
Zustand SLEEP aus einem Timer-Wakeup kommt (so wie ein Wecker, der nur im 10-min-Fenster auslöst):
z. B. Startfenster 7:00–7:10 analog `ALARM_GRACE_MINUTES`, danach nur noch per Bedienung. Aufwand S.

### 3. 🔴 Nach Stromausfall: alte Uhrzeit und nächster Versuch erst nach 5 h

**Stellen:** `src/wifi_controller.cpp:527–541` (`initClock`), `:596–604` (`syncFailed`), `:348–352`
(`nextSyncIntervalMs`), `:382` und `:398–400` (Zeit nur alle 30 min und nur bei NTP gesichert)

**Problem:**
- Nach dem Einschalten wird die Uhr aus `lastKnownTime` gesetzt. Dieser Wert ist bis zu 30 min älter als
  der Ausfallzeitpunkt **plus die Dauer des Stromausfalls**. Die Zeit gilt trotzdem als „gültig“
  (≥ 2020) → Wecker und Feier-Modus rechnen mit der falschen Zeit.
- Bei einem Stromausfall startet meist auch der Router neu (1–3 min). Der erste Abgleich bricht nach
  `STA_SYNC_TIMEOUT_MS` = 1 min ab → `s_lastSyncOk = false` → **nächster Versuch erst nach `syncRetryMin`
  (Standard 300 min)**. TSF hilft hier nicht: Alle Router haben ihren Zähler zurückgesetzt → nur Rebase,
  keine Schätzung.
- TSF- und RDS-Erfolge sichern die Zeit nicht in `lastKnownTime`; vor dem Deep Sleep wird sie auch nicht
  gesichert.

**Szenario:** Stromausfall 3 h nachts → Lampe startet mit 3,5 h zu früher Zeit → Router noch nicht da →
nächster Versuch in 5 h → **Wecker 7:00 klingelt um ca. 10:30**.

**Lösung:**
1. Herkunft der Uhrzeit mitführen (`TIME_SRC_NONE | NVS | TSF | RDS | NTP`, im RTC-Speicher).
2. Solange die Zeit nur aus dem NVS stammt: **gestaffelte Wiederholung** 1 → 2 → 5 → 10 → 30 min, erst
   danach `syncRetryMin` (z. B. `nextSyncIntervalMs()` gibt bei `TIME_SRC_NVS` den Backoff-Wert zurück).
   Deep Sleep schläft entsprechend kürzer.
3. `saveLastKnownTime()` auch nach TSF/RDS-Erfolg und direkt vor `esp_deep_sleep_start()` aufrufen
   (ein 8-Byte-Wert, unkritisch für den Flash).
4. Optional: Bei `TIME_SRC_NVS` Systemwarnung (gelb) zeigen, wenn die Lampe an ist — dann sieht man, dass
   die Uhr unsicher ist.

Aufwand M.

### 4. 🔴 `app.js` wird ein Jahr lang gecacht — nach einem UI-Update läuft die alte Oberfläche

**Stelle:** `src/route_handlers.cpp:109` (`Cache-Control: public,max-age=31536000`), Einbindung in
`svelte-ui/index.prod.html:20` (`<script src="/app.js">` ohne Versionsangabe)

**Problem:** `index.html` wird nie gecacht, `app.js` aber 1 Jahr unter **unveränderter URL**. Nach
`./deploy.sh` lädt ein Handy, das die Seite schon kannte, weiter das alte Bundle aus dem Cache. Die alte
UI spricht dann mit der neuen Firmware-API (neue/umbenannte Felder, neue Endpunkte) → Fehler oder stilles
Fehlverhalten, das schwer zu finden ist.

**Lösung (eine davon):**
- **Empfohlen:** In `svelte-ui/build-esp32.js` den Inhalt-Hash von `app.js` berechnen und in
  `index.html` als `/app.js?v=<hash>` einsetzen. Die Firmware ignoriert den Query-Parameter; die lange
  Cache-Dauer bleibt erhalten, jede neue Version hat eine neue URL.
- Alternativ `Cache-Control: no-cache` + `ETag`/`Last-Modified` — dann fragt der Browser jedes Mal nach
  (kleine Mehrlast, kein Hash nötig).

Aufwand S.

### 5. 🟠 Automatische Abfragen der UI verhindern das Abschalten des Hotspots

**Stellen:** `src/captive_portal.cpp:15–19` (`markActivity`), `svelte-ui/src/App.svelte:37`
(`syncActiveSet()` → `GET /get_config` alle 10 s), `svelte-ui/src/components/SystemMonitor.svelte:11`
(`/get_system_info` alle 5 s)

**Problem:** Laut Kommentar sollen automatische Abfragen die Hotspot-Laufzeit **nicht** verlängern.
Ausgenommen sind aber nur `/ping`, `/get_event_log` und `/get_rds_clock`. Die Hauptseite ruft alle 10 s
`/get_config` ab, der Systemmonitor alle 5 s `/get_system_info` — beides zählt als Bedienung.

**Szenario:** Handy liegt mit offener Lampenseite auf dem Nachttisch → `apTimeoutMin` läuft nie ab →
Hotspot bleibt an → **kein Deep Sleep**, Funk dauerhaft an.

**Lösung:** Statt einer Ausnahmeliste eine **Positivliste**: Bedienung = alle POSTs + `GET /`. Oder die UI
kennzeichnet Hintergrundabfragen mit einem Header (`X-Background: 1`), `markActivity()` ignoriert diese.
Die Positivliste ist robuster, weil neue Polling-Endpunkte automatisch richtig behandelt werden. Aufwand S.

---

## B. Nebenläufigkeit (Webserver-Task ↔ `loop()`)

Die Architektur „Handler legen nur Aufträge ab“ ist gut. An drei Stellen wird sie noch unterlaufen.

### 6. 🟠 Verlorene Änderungen bei schnellen `/set_config` + Lade-Rennen in der UI

**Stellen:** `src/route_handlers.cpp:176` (`FullConfig newConfig = *g_config;`),
`src/web_commands.cpp:70–73` (Auftrag ersetzt den vorigen), `svelte-ui/src/stores/configStore.js:54–65`
(`post()` lädt sofort danach neu)

**Problem (Firmware):** Ein Teil-Update wird auf dem **aktuell angewendeten** `appConfig` aufgebaut,
nicht auf einem noch ausstehenden Auftrag. Kommen zwei POSTs, bevor `loop()` den ersten verarbeitet hat
(z. B. während eines NVS-Schreibvorgangs, eines Kanal-Scans oder einer TSF-Berechnung), basiert der zweite
auf dem alten Stand und **überschreibt den ersten** („letzter gewinnt“).

**Problem (UI):** Jede Änderung sendet `POST /set_config` und direkt danach `GET /get_config`. Der GET kann
beantwortet werden, bevor `loop()` den Auftrag übernommen hat → die UI zeigt kurz den alten Stand
(Eingabe „springt zurück“). Mehrere schnelle Änderungen laufen parallel; die Antworten können in falscher
Reihenfolge ankommen und neuere Eingaben mit älteren Gerätedaten überschreiben.

**Lösung:**
- Firmware: Basis für das Teil-Update = ausstehender Auftrag, falls vorhanden (unter dem Mutex lesen),
  sonst `appConfig`. Sauberer: den JSON-Text als Auftrag ablegen und **in `loop()` parsen** (dann gibt es
  nur eine Quelle der Wahrheit). Zusätzlich eine Revisionsnummer `configRev` mitführen, die in
  `/get_config` steht.
- UI: POSTs serialisieren (Promise-Kette), Neuladen nur nach dem letzten POST einer Folge, und nur
  übernehmen, wenn `configRev` ≥ erwarteter Stand.

Aufwand M.

### 7. 🟠 Webhandler lesen globalen Zustand ohne Sperre

**Stellen:** `src/route_handlers.cpp:158` (`createConfigJson(*g_config)`), `:176`, `:195`, `:213`;
`src/tsf_clock.cpp:604–649` (`getTsfStatusJson` liest `g_storedEntries`); `src/clock_sync.cpp:28–30`;
`src/wifi_controller.cpp:335–352` (`msUntilNextSync` liest `externalSSID` per `strlen`)

**Problem:** Der async_tcp-Task kopiert/serialisiert `FullConfig`, `SystemSettings` und die TSF-Tabelle,
während `loop()` sie per `memcpy`/Zuweisung ändert. Ergebnis sind „zerrissene“ Lesevorgänge: z. B. ein
SSID-Puffer, in dem die alte Endnull schon überschrieben, die neue aber noch nicht geschrieben ist →
`strlen` läuft ins nächste Feld. Bei `/set_config` und `/set_system_config` wird so ein halber Stand sogar
zur Basis des nächsten Auftrags.

**Lösung:** Snapshot-Muster — `loop()` veröffentlicht nach jeder Änderung eine Kopie unter einem Mutex,
Handler lesen nur diese Kopie:

```cpp
// Gemeinsamer, gesperrter Schnappschuss (z. B. in shared_state.h)
template<typename T> class Snapshot {
    T data; SemaphoreHandle_t m = xSemaphoreCreateMutex();
public:
    void publish(const T& v) { xSemaphoreTake(m, portMAX_DELAY); data = v; xSemaphoreGive(m); }
    T read() { xSemaphoreTake(m, portMAX_DELAY); T c = data; xSemaphoreGive(m); return c; }
};
```

Die Speicherkosten sind klein (≈ 200 B `FullConfig`, ≈ 400 B `SystemSettings`, 1 KB TSF). Für die
skalaren Status-Felder (`WiFiTestTracker`, `s_lastSyncOk` …) reicht ein Status-Struct, das `loop()` einmal
pro Sekunde in denselben Snapshot schreibt. Aufwand M.

### 8. 🟠 I²C- und WLAN-Treiberaufrufe aus dem Webserver-Task

**Stellen:** `src/rds_clock.cpp:365–366` (`getRssi()` → I²C in `getRdsStatusJson`),
`src/system_info.cpp:111–112` (`WiFi.status()`, `WiFi.RSSI()`, `esp_wifi_ap_get_sta_list`),
`src/clock_sync.cpp:29` (`isStaActive()` → `WiFi.getMode()`)

**Problem:** Verstößt gegen die eigene Regel „Handler nur prüfen, ablegen, antworten“. `/get_rds_clock`
wird alle 10 s abgefragt, während `loop()` im Zustand LOCKED alle 100 ms dieselben Register liest — zwei
Tasks teilen sich den I²C-Bus. Ein hängender I²C-Zugriff blockiert den async_tcp-Task (alle Clients
hängen). WLAN-Abfragen können mit `WiFi.mode(…)` in `loop()` kollidieren (Treiber wird gerade gestoppt).

**Lösung:** `loop()` cached die Werte (RDS-RSSI bei jedem Lesen, WLAN-RSSI/Clients/Modus einmal pro
Sekunde) im Status-Snapshot aus Punkt 7; die Handler lesen nur den Cache. Aufwand S.

### 9. 🟠 Kein Watchdog für `loop()`

**Stellen:** `src/main.cpp:60–108` (kein `enableLoopWDT()`), Framework-Standard
`loopTaskWDTEnabled = false` (`framework-arduinoespressif32/cores/esp32/main.cpp:111`);
`portMAX_DELAY` in `src/web_commands.cpp:68`, `src/event_log.cpp:40`, `src/wifi_controller.cpp:738ff`

**Problem:** Hängt `loop()` (Deadlock, I²C-Hänger, Endlosschleife), merkt das niemand: Der Leerlauf-Task
bekommt weiter Rechenzeit, der Idle-Watchdog schlägt nicht an. Für einen **Wecker** ist das der
schlimmste Fall — die Lampe bleibt stumm, bis jemand den Stecker zieht.

**Lösung:**
- In `setup()` `enableLoopWDT()` (bzw. `esp_task_wdt_add(nullptr)` mit z. B. 15–30 s Timeout); die längste
  legitime Blockade ist der Kanal-Scan in `bestChannel()` (≈ 2–3 s) und `probeCrystal()` (≈ 0,3 s).
- Mutex-Wartezeiten im async_tcp-Pfad begrenzen (z. B. 100 ms) und bei Zeitüberschreitung mit 503
  antworten, statt ewig zu warten.
- Der Reset-Grund `task_wdt` wird schon in `/get_system_info` angezeigt — zusammen mit Punkt 16 sieht man
  solche Hänger dann auch nachträglich.

Aufwand S.

---

## C. Robustheit und Fehlertoleranz

### 10. 🟠 RDS-Uhr und Deep Sleep passen nicht zusammen

**Stellen:** `src/sleep_manager.cpp:58–125` (prüft RDS nicht), `src/rds_clock.cpp:52–57` (Zustand und
Zeitgeber nur im RAM, `millis()`-basiert), `:264–269`

**Problem:** Ein RDS-Abgleich braucht 35–130 s pro Sender, ein voller Bandscan Minuten. `checkToGoSleep()`
schaut nur auf WLAN, nicht auf RDS — sobald das WLAN aus ist, schläft die Lampe nach spätestens 10 s ein,
mitten im Scan. Nach dem Aufwachen beginnt der Zustandsautomat wieder bei `RDS_STATE_OFF` (die
1-h-/24-h-Wartezeiten zählen in `millis()` und sind weg) → bei jedem Aufwachen ein neuer, wieder
abgebrochener Versuch. RDS funktioniert in der Praxis nur bei abgeschaltetem Deep Sleep.

**Lösung:**
- `isRdsBusy()` anbieten; `checkToGoSleep()` wartet darauf, aber höchstens ein Zeitbudget (z. B. 3 min
  pro Wachphase), damit RDS nicht den Akku-/Schlafbetrieb aushebelt.
- „Letzter Versuch“ und „letzter Erfolg“ als Unix-Zeit im RTC-Speicher (`RTC_DATA_ATTR`) statt
  `millis()`; RDS nur starten, wenn fällig.
- Gespeicherten Kanal zuerst probieren (passiert schon) und nur bei Misserfolg den Bandscan auf mehrere
  Wachphasen verteilen (Startkanal im RTC-Speicher weiterzählen).

Aufwand M.

### 11. 🟠 Warnung „Zeit veraltet“ geht mit jedem Deep Sleep verloren

**Stelle:** `src/lamp_state.cpp:193–204`

**Problem:** Das Alter der Zeitquelle wird über `millis() - lastSucceededTestTime` bestimmt. Beides
beginnt nach jedem Deep Sleep bei 0. Im Normalbetrieb mit Deep Sleep erscheint die gelbe Warnung deshalb
praktisch nie, selbst wenn tagelang kein Abgleich geklappt hat — die Lampe müsste dafür 30 min am Stück
wach sein.

**Lösung:** Die Unix-Zeitpunkte aus `clock_sync.cpp` (liegen schon im RTC-Speicher) nutzen:
`now - max(ntpAt, rdsAt, tsfAt) > 24 h` → Warnung. Nach Stromausfall (RTC-Speicher leer) greift die
Herkunft „NVS“ aus Punkt 3. Aufwand S.

### 12. 🟠 Werksreset per BOOT-Taste schon nach 1 s

**Stelle:** `src/button.cpp:28` (`longPressAfterMiliseconds = 1000`), `:117–127`

**Problem:** 1 s Halten der BOOT-Taste löscht **alle** Einstellungen inkl. WLAN-Daten — ohne Vorwarnung.
Ein versehentlicher Druck (Gehäuse, Kinderhand, beim Anstecken) reicht. Außerdem blockiert der Zweig
`loop()` mit `delay(1000)`.

**Lösung:** 8–10 s Halten mit sichtbarem Countdown (z. B. rot blinkend, schneller werdend); ausgelöst wird
erst nach Ablauf, Loslassen vorher bricht ab. Danach Reset über denselben verzögerten Pfad wie
`/factory_reset` (`queueWebCommand(STATE_CHANGE_FACTORY_RESET)`), statt `delay()` + `ESP.restart()` direkt
im Callback. Aufwand S.

### 18. 🟢 Aufwecken per Knopfdruck kann sofort „Gute Nacht“ auslösen 🔍

**Stellen:** `src/main.cpp:42–47` (Wakeup → Stufe 1), `src/button.cpp:168–202`, `:137–157`

**Problem:** Die Lampe ist nach ca. 0,35 s bereit. Hält man den Knopf beim Aufwecken länger, sieht
`process_button()` beim ersten Durchlauf „frisch gedrückt“, beim Loslassen einen **kurzen Klick**
(50–500 ms) → `onShortPress()` im Normalbetrieb → **Gute Nacht** (grüner Blitz, Lampe dimmt ab).

**Lösung:** In `initRotaryEncoder()`: ist die Taste beim Start gedrückt, diesen Druck als „schon
behandelt“ markieren (`wasButtonDown = true`, `longPressHandled = true`). Am Gerät mit einem gehaltenen
Weck-Druck (~0,5–0,8 s) prüfen. Aufwand S.

### 19. 🟢 TSF-Erfassung bleibt an, wenn TSF währenddessen abgeschaltet wird

**Stelle:** `src/tsf_clock.cpp:565–567`

**Problem:** `tsfClockLoop()` kehrt bei abgeschaltetem TSF sofort zurück — läuft gerade eine Erfassung,
wird `stopCapture()` nie aufgerufen. Der Promiscuous-Modus bleibt an, `g_captureRunning` bleibt `true`.

**Lösung:** Vor dem frühen `return`: `if(g_captureRunning) stopCapture();` und Auslöser
(`g_ntpSyncedTrigger`, `g_fallbackTrigger`, `*Scheduled`) zurücksetzen. Aufwand S.

### 20. 🟢 `FullConfig` wird nach dem Laden nicht plausibilisiert

**Stellen:** `src/store.cpp:293–299` (`loadFullConfigOrDefault`), im Gegensatz zu `:302–320`
(`SystemSettings` wird geprüft)

**Problem:** `setCount` 0 oder > 6, `activeSet ≥ setCount`, Wecker-Felder außerhalb des Bereichs werden
ungeprüft übernommen. Einige Stellen sichern sich selbst ab (`checkAndApplyColorMode`), andere nicht
(`createConfigJson` gibt dann z. B. 0 Sets aus, die UI kann damit nichts anfangen). Bei
`SystemSettings` wird nur `lampName` terminiert — die SSID-/Passwort-Puffer nicht.

**Lösung:** Eine Funktion `sanitizeFullConfig()` (Werte klemmen, nicht migrieren) nach dem Laden und
nach jedem Parse aufrufen; in `loadSystemSettingsOrDefault()` alle `char[]`-Felder terminieren
(`buf[sizeof(buf) - 1] = '\0'`). Aufwand S.

### 21. 🟢 WLAN-Nachrichten-Queue verwirft still

**Stellen:** `src/wifi_controller.cpp:155–160`, `:193` (Queue-Länge 8)

**Problem:** Ist die Queue voll (z. B. `loop()` hängt länger in einem Scan, während der Treiber mehrere
Disconnect-Events schickt), geht **die neueste** Nachricht verloren — womöglich `MSG_NTP_SYNCED` oder
`MSG_GOT_IP`. Dann gilt ein erfolgreicher Abgleich als gescheitert (Timeout nach 1 min), und das Heimnetz
wird nicht getrennt.

**Lösung:** Queue auf 16–32 vergrößern (Elemente sind 8 Byte), verworfene Nachrichten zählen und im
Systemmonitor anzeigen. Aufwand S.

### 24. 🟢 Einmaliger Wecker akzeptiert ungültige Daten

**Stelle:** `src/json_utils.cpp:134`

**Problem:** Geprüft wird nur Tag 1–31. Ein „31.02.“ wird gespeichert; `checkAlarmStates()` findet nie
einen 31. Februar (Wecker klingelt nie), `calculateMillisToNextAlarm()` normalisiert per `mktime` auf den
3. März (Deep Sleep wacht dafür auf). Uneinheitlich und für den Nutzer unsichtbar.

**Lösung:** Tage pro Monat inkl. Schaltjahr prüfen und mit Meldung ablehnen. Aufwand S.

---

## D. Ressourcen und Skalierung

### 13. 🟠 Ereignisprotokoll als ein großer String

**Stellen:** `src/event_log.cpp:59–102`, `:8–20`

**Problem:** Jeder Abruf von `/get_event_log` (alle 10 s pro offenem Tab) belegt ≈ 12 KB (Kopie) plus
≈ 18 KB (`String` mit `reserve`) am Stück. Mit mehreren Clients und fortschreitender Heap-Fragmentierung
sinkt `heapMaxAlloc` — dann schlägt `malloc` fehl (Antwort leer) oder eine andere Allokation im
WLAN-Stack. Außerdem: `s_head`/`s_count` sind `uint8_t`; wer `MAX_LOG_EVENTS` auf > 255 erhöht, bekommt
ohne Warnung einen kaputten Ringpuffer.

**Lösung:**
- Antwort **streamen** (`request->beginChunkedResponse(…)` oder `AsyncResponseStream`), Eintrag für
  Eintrag aus dem Ringpuffer — kein großer Puffer mehr.
- Fortlaufende Sequenznummer je Eintrag und `GET /get_event_log?since=<seq>`: die UI holt nur Neues.
- `static_assert(MAX_LOG_EVENTS <= 255, "Index ist uint8_t")` oder Indizes auf `uint16_t`.

Aufwand M.

### 14. 🟠 Polling statt Push — skaliert schlecht mit mehreren Clients

**Stellen:** `svelte-ui/src/App.svelte:48` (`/ping` + `/get_config` alle 10 s),
`components/SystemMonitor.svelte:11` (5 s), `components/EventLog.svelte:79` (10 s),
`Settings.svelte:78` (RDS 10 s), WLAN-Suche/-Test (1 s)

**Problem:** Pro offenem Tab bis zu ≈ 0,6 Anfragen/s, jede mit `String` + `JsonDocument` auf dem Heap.
Bei 4 Hotspot-Clients (`MAX_CLIENTS`) und eventuell einem Heimnetz-Client vervielfacht sich das. Neue
Funktionen bringen erfahrungsgemäß neue Polling-Endpunkte mit. Gleichzeitig sind Änderungen am
Drehknopf in der UI bis zu 10 s verzögert.

**Lösung:** **Server-Sent Events** (`AsyncEventSource`, in ESPAsyncWebServer enthalten): `loop()` sendet
bei Änderung kleine Ereignisse (`state`, `activeSet`, `brightness`, `log`, `sync`), die UI hört zu und
fragt nur noch beim Verbindungsaufbau den vollen Stand ab. Polling bleibt als Fallback (z. B. 30 s), falls
der Captive-Portal-Browser SSE nicht kann. Zwischenschritt ohne SSE: einen kombinierten Endpunkt
`/get_state` mit Revisionsnummer, der bei unverändertem Stand nur `304`/`{"rev":n}` liefert. Aufwand L.

### 15. 🟠 Kein OTA, UI und Firmware getrennt geflasht, keine Versionsprüfung

**Stellen:** `platformio.ini` (`min_spiffs.csv`: `app0`/`app1` je 1,9 MB = OTA-fähig, Dateisystem nur
128 KB), `deploy.sh` (`uploadfs` und `upload` getrennt)

**Problem:**
- Jedes Update braucht USB und einen Rechner mit PlatformIO — bei mehreren Lampen oder einer Lampe im
  Kinderzimmer der Engpass.
- Firmware und Dateisystem werden getrennt geschrieben; schlägt einer der beiden Schritte fehl oder wird
  nur `./build.sh firmware` + `upload` gemacht, passen UI und API nicht zusammen. Es gibt keine
  Versionsnummer, die das erkennen ließe.
- Platz: Firmware 1,29 MB von 1,9 MB (67 %), `app.js.gzip` 52 KB im 128-KB-Dateisystem — für die nächsten
  Ausbaustufen noch ok, aber beobachten.

**Lösung:**
1. Build-Version (`git describe --dirty`) per `build_flags` (`-DFW_VERSION=…`, z. B. über ein
   `extra_scripts`-Skript) in die Firmware und per Vite (`define`) in die UI. `/get_system_info` liefert
   `fwVersion`, die UI vergleicht mit der eigenen und zeigt bei Abweichung einen Hinweis.
2. OTA-Endpunkt `POST /update` (Arduino `Update.h`) für Firmware **und** LittleFS-Image, nur über den
   Hotspot (siehe Punkt 28), mit Abbruch bei falscher Größe/Prüfsumme. Die Partitionstabelle passt schon.
3. Alternativ zu getrennten Images: UI als `PROGMEM`-Array in die Firmware einbetten → nur noch **ein**
   Image, UI und API sind immer gleich alt (kostet ≈ 65 KB Flash, ist reichlich vorhanden).

Aufwand M (Version) bis L (OTA).

### 16. 🟠 Keine Absturzdiagnose über Neustarts hinweg

**Stellen:** `src/event_log.cpp:18` (Ringpuffer im normalen RAM), Partitionstabelle mit `coredump`-Partition
(64 KB, ungenutzt)

**Problem:** Das Ereignisprotokoll ist nach jedem Neustart **und jedem Deep Sleep** leer — genau nach einem
Absturz (`panic`, `task_wdt`, `brownout`) fehlt die Vorgeschichte. Man sieht nur den Reset-Grund.

**Lösung:**
- Die letzten ≈ 20 Einträge zusätzlich in einem `RTC_NOINIT_ATTR`-Puffer halten (übersteht Software-Reset,
  Watchdog und Panic, nicht Stromausfall) und nach dem Start ins Protokoll übernehmen („vor Neustart: …“).
- Core-Dump in die vorhandene `coredump`-Partition aktivieren (sdkconfig/`board_build`-Option prüfen) und
  eine Kurzfassung (PC, Task, Backtrace-Adressen) über die API ausgeben; lässt sich am Rechner mit
  `xtensa/riscv32-…-addr2line` bzw. `espcoredump.py` auswerten.
- Brownout zählen: 16 RGBW-LEDs auf Stufe 7 ziehen > 1 A — ein Brownout-Neustart beim Weckverlauf wäre so
  sofort sichtbar.

Aufwand M.

### 17. 🟠 Keine automatischen Tests / CI

**Stelle:** `test/` enthält nur die README

**Problem:** Die fehleranfälligste Logik ist rein rechnerisch und ohne Hardware testbar, wird aber nur am
Gerät geprüft: nächste Weckzeit inkl. Sommer-/Winterzeit (`calculateMillisToNextAlarm`), Schlafdauer mit
Drift-Vorlauf (`checkToGoSleep`), TSF-Median/Ausreißer/Gang (`processFallback`, `updateDrift`),
JSON-Validierung (`parseConfigJson`, `parseSystemConfigJson`), Party-Tage, `millis()`-Überlauf.
Jede Erweiterung riskiert unbemerkte Rückschritte (Punkte 1, 2, 24 wären per Test aufgefallen).

**Lösung:**
- PlatformIO-Umgebung `[env:native]` mit Unity; die reinen Rechenfunktionen von Arduino-Abhängigkeiten
  trennen (Zeit als Parameter statt `time()`/`millis()` im Inneren).
- GitHub Actions: `pio run -e esp32c3`, `pio test -e native`, `npm ci && npm run build` in `svelte-ui`.
- UI: Mock-Daten (`mockData.js`) gegen echte Firmware-Antworten prüfen (ein JSON-Schnappschuss je
  Endpunkt im Repo, Test vergleicht die Felder) — sonst laufen Mock und API auseinander.

Aufwand M–L.

### 22. 🟢 Grenzwerte mehrfach im Code verteilt

**Stellen:** `svelte-ui/src/stores/configStore.js:7,11` (`MAX_ALARMS`, `MAX_LIGHT_SETS` „muss zur Firmware
passen“), Meldungstexte mit festen Zahlen (`src/route_handlers.cpp:92` „1–16“, `src/json_utils.cpp:78`
„1–6“, `src/captive_portal.cpp:116` „4096“), `MAX_BODY_SIZE` 4096 vs. Größe von `/set_config`

**Problem:** Wer eine Grenze ändert, muss Firmware, UI und Texte synchron anpassen. `/set_config` sendet
immer alle 16 Wecker (≈ 1,6 KB); bei 32+ Weckern oder mehr Feldern stößt der Body an die 4096-Byte-Grenze
und wird mit 413 abgelehnt — ohne dass es vorher auffällt.

**Lösung:** Firmware liefert die Grenzen mit (`/get_config` → `"limits": {"maxAlarms":16, "maxSets":6,
"maxLeds":16, …}`), die UI liest sie statt eigener Konstanten; Meldungen per `snprintf` mit den
Konstanten bauen. `static_assert` auf eine Größenabschätzung der Config-JSON < `MAX_BODY_SIZE`.
Hinweis: Eine geänderte Grenze ändert die Strukturgröße → Einstellungen werden beim Update zurückgesetzt
(bewusst so gewollt, siehe unten). Aufwand M.

### 23. 🟢 Mehrere Lampen: gleicher Hotspot-Name, Personendaten zur Compile-Zeit

**Stellen:** `src/store.cpp:174` (`SECRET_AP_SSID`), `include/secrets.example.h`

**Problem:** Alle Lampen aus demselben Build haben denselben Hotspot-Namen — zwei Lampen nebeneinander
sind am Handy nicht zu unterscheiden. Name und Party-Tage kommen als Standard aus `secrets.h`, also pro
Lampe ein eigener Build.

**Lösung:** Standard-SSID mit den letzten 2 Byte der MAC ergänzen (`"Lampe-3A4F"`), solange der Nutzer
keinen eigenen Namen gesetzt hat. Personendaten nur noch als leere/neutrale Standardwerte und über den
Hotspot einrichten (die UI kann das bereits) — dann reicht **ein** Build für alle Lampen. Aufwand S–M.

### 25. 🟢 Mehrfache NVS-Schreibvorgänge pro Auftrag

**Stellen:** `src/web_commands.cpp:199–243` (je Schalter ein `saveSystemSettings`), `src/rds_clock.cpp:207–211`

**Problem:** Kommen mehrere Schalter in einem `loop()`-Durchlauf an, wird `SystemSettings` mehrfach
komplett geschrieben; RDS schreibt bei jedem gefundenen Sender. Unkritisch für die Lebensdauer, aber
unnötige Blockaden von `loop()` (NVS-Schreiben = einige ms, beim Seitenwechsel im Flash mehr).

**Lösung:** Dirty-Flag setzen und am Ende von `processWebCommands()` einmal speichern; für
`SystemSettings` dasselbe verzögerte Speichern wie für `FullConfig` (`checkToSave`). Aufwand S.

### 26. 🟢 Logging baut Strings auch bei abgeschaltetem Log

**Stellen:** `src/debug_utils.cpp:18`; Aufrufe wie `serialPrint("…" + String(x) + …)` u. a. in
`setBrightnessLevel`, Handlern, `checkToGoSleep`; außerdem im WiFi-Event-Task
(`src/wifi_controller.cpp:672–696`, obwohl der Kommentar „kein Logging“ sagt)

**Problem:** Die `String`-Verkettung passiert vor dem Aufruf — auch wenn serielle Logs aus sind. Viele
kleine Heap-Allokationen in häufig laufenden Pfaden fördern Fragmentierung.

**Lösung:** Makro mit `printf`-Format und früher Prüfung:

```cpp
#define LOGF(fmt, ...) do { if(serialLogEnabled()) serialPrintf(fmt, ##__VA_ARGS__); } while(0)
```

`serialPrintf` formatiert in einen Stack-Puffer (z. B. 128 Byte). Schrittweise umstellen. Aufwand S
(Makro) + laufend.

### 27. 🟢 Nicht-statische globale Namen

**Stellen:** `src/alarm.cpp:29,40` (`alarms`, `msToNextAlarm`), `src/wifi_controller.cpp:138–139`
(`dnsServer`, `server`), `src/button.cpp:38` (`rotaryEncoder`), `src/led.cpp:22` (`ws2812fx`)

**Problem:** Mit wachsendem Code steigt die Gefahr von Namenskollisionen (Linker-Fehler oder — schlimmer —
versehentlicher Zugriff aus einem anderen Modul).

**Lösung:** `static` bzw. anonymer Namespace; Zugriff nur über Funktionen. Aufwand S.

---

## E. Sicherheit (soweit sie Stabilität betrifft)

### 28. 🟠 API ohne Schutz — auch im Heimnetz erreichbar

**Stellen:** `src/wifi_controller.cpp:438–442` (Server lauscht auf allen Schnittstellen),
`doc/architektur.md` („Schutz bietet das Hotspot-Passwort“)

**Problem:** Während jedes Zeitabgleichs (STA, ≈ 1 min alle 5 h, bei Tests länger) ist die **komplette
API** im Heimnetz erreichbar — inkl. `/factory_reset`, `/restart`, `/set_system_config`. Der Schutz über
das Hotspot-Passwort greift dort nicht. Ein leeres Hotspot-Passwort ist erlaubt (offener Hotspot).

**Lösung:** In den Handlern (zentral in `captive_portal.cpp`) nur Anfragen annehmen, deren lokale Adresse
die Hotspot-IP ist (`request->client()->localIP() == 4.3.2.1`), sonst 403. Optional später eine PIN für
ändernde Endpunkte (Werksreset, WLAN, OTA). Aufwand S (Schnittstellenfilter) bis M (PIN).

---

## Bewusst nicht aufgenommen

- **Versionierung/Migration der NVS-Strukturen:** Nach eurer Entscheidung ist ein Reset der
  Einstellungen beim Update in Ordnung. Das TODO in der `README.md` („Versionsfeld + Migration“) widerspricht
  dem — es sollte angepasst oder gestrichen werden, damit niemand es später umsetzt.
- **`millis()`-Überlauf nach 49,7 Tagen:** Fast alle Stellen rechnen überlaufsicher per Subtraktion.
  Einzige Ausnahme `src/led.cpp:101` (`millis() >= s_flashEndMs`) — im Überlauf-Moment endet ein Blitz
  höchstens sofort, kein Hänger. Bei der Umstellung auf das Subtraktionsmuster mitnehmen.
- **NVS-Verschleiß:** Helligkeit wird 5 s entprellt, die Uhrzeit nur alle 30 min gesichert, TSF alle 5 h —
  unkritisch.

---

## Fahrplan

Empfohlene Reihenfolge — jeweils ein kleiner, eigener Commit:

1. **Schnelle Fehlerbehebungen (je S):** 1 (Wecker RTC), 2 (Feier RTC), 4 (Cache-Hash), 5 (Positivliste
   Bedienung), 9 (Loop-Watchdog), 19 (TSF stoppen), 12 (BOOT-Reset 10 s)
2. **Zeit-Robustheit:** 3 (Herkunft der Zeit, Backoff nach Stromausfall), 11 (Warnung über RTC-Zeitpunkte),
   10 (RDS mit Deep Sleep)
3. **Nebenläufigkeit:** 7 (Snapshot-Muster) → 8 (Caches) → 6 (Revision + serialisierte UI-POSTs)
4. **Wartbarkeit:** 17 (native Tests + CI), 15 Teil 1 (Versionsnummer in Firmware und UI), 16 (Diagnose)
5. **Skalierung:** 13 (Log streamen), 14 (SSE), 22 (Grenzen aus der Firmware), 23 (eine Firmware für alle
   Lampen), 15 Teil 2 (OTA), 28 (Schnittstellenfilter)
