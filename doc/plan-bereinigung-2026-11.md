# Plan: Projekt auf sauberen, stabilen Stand bringen

_Stand: 19.11.2026 — Grundlage: kompletter Code-Durchgang (Firmware + Svelte-UI + Build-Skripte),
inkl. der noch nicht committeten Änderungen auf `cleanup2026`. Firmware baut fehlerfrei
(RAM 16,9 %, Flash 60,3 %). Nichts am Gerät getestet._

Legende: **[sicher]** = im Code eindeutig nachvollziehbar · **[prüfen]** = hängt von Hardware/Bibliothek ab,
am Gerät verifizieren.

---

## Teil 1 — Fehler und mögliche Probleme

### A. Kritisch (falsches Verhalten im Alltag)

| ID | Problem | Ort | Folge | Fix |
|----|---------|-----|-------|-----|
| K1 | **NTP-Status flackert.** `sntp_get_sync_status()` liefert `COMPLETED` nur **einmal** und setzt sich danach selbst auf `RESET` (laut IDF-Doku). [sicher] | `wifi_controller.cpp:251`, `:384` | `clockSynced` ist nach einer Minute wieder `false` → Log „NTP sync lost“, UI zeigt „nicht synchronisiert“, TSF-Fallback läuft los, obwohl die Zeit stimmt. | Sync-Status im `MSG_NTP_SYNCED`-Zweig setzen (`clockSynced`, `lastSucceededTestTime`), eigenes Flag statt Polling. |
| K2 | **Hotspot-Einstellungen können das Gerät aussperren.** Leere AP-SSID oder AP-Passwort < 8 Zeichen → `softAP()` schlägt fehl. [sicher] | `json_utils.cpp:128`, `wifi_controller.cpp:97` | Kein Hotspot mehr, Weboberfläche unerreichbar — nur Boot-Taste (Reset) hilft. | Firmware: SSID 1–32, PW leer oder 8–63 Zeichen, sonst 400. UI: gleiche Prüfung vor dem Senden. |
| K3 | **Drehen beendet Gute-Nacht nicht.** Zustand wird auf DEFAULT gesetzt, `goodNightModeActive` bleibt aber `true`. [sicher] | `button.cpp:77` | Nächste Loop-Runde → sofort wieder GOOD_NIGHT, Helligkeit springt. | `stopGoodNightMode()` aufrufen. |
| K4 | **Drehen im SLEEP-Zustand.** LEDs gehen an, Zustand bleibt SLEEP. [sicher] | `button.cpp:56ff`, `sleep_manager.cpp:49` | Deep Sleep wird trotzdem eingeleitet; WS2812 halten ihre Farbe → Lampe leuchtet stundenlang, ESP schläft. | Drehen in SLEEP → DEFAULT. Zusätzlich in `enterDeepSleep()` LEDs explizit löschen. |
| K5 | **Systemwarnung schaltet ausgeschaltete Lampe ein.** SYS_WARNING/SYS_ERROR setzen Helligkeit 7, wenn Nutzer-Helligkeit 0 ist; seit dem WIP-Umbau leuchtet die **ganze** Lampe gelb/rot. [sicher] | `lamp_state.cpp:196–203` | Nachts nach 24 h ohne NTP: Lampe geht gelb an. Ohne externes WLAN: nach 30 min dauerhaft gelb. Verhindert außerdem Deep Sleep. | Warnung nur anzeigen, wenn Lampe an ist (oder nur Status-LED); TSF/RDS-Zeit als gültige Quelle werten. |
| K6 | **Alerts heben SLEEP auf.** Nach einem Alert (langer Druck, Einstellungen speichern) fällt der Zustand auf DEFAULT. `lastNormalState` wird nie gesetzt. [sicher] | `lamp_state.cpp:76–87`, `:238` | Lampe im Schlafmodus geht nach Alert an. | Zustand vor dem Alert merken und danach wiederherstellen; `lastNormalState` korrekt pflegen oder entfernen. |
| K7 | **Race Conditions Web-Task ↔ `loop()`** (aus Review 17.11., weiterhin offen). Web-Handler laufen im `async_tcp`-Task und rufen dort `ws2812fx`, `stopWifi()/startWifi()`, `Preferences`, `delay()`+`ESP.restart()` auf. [sicher] | `main.cpp:178ff`, `route_handlers.cpp` | Zufällige LED-Störungen, Abstürze, verlorene Antworten. | Handler legen nur Aufträge in eine Queue; `loop()` führt sie aus (Restart/WLAN-Neustart verzögert). |
| K8 | **UI überschreibt die Helligkeit.** Jede UI-Änderung (Farbe, Wecker …) schickt die komplette Config inkl. `brightnessMode` vom letzten Laden. [sicher] | `configStore.js:57`, `json_utils.cpp:59` | Lampe per Drehknopf ausgeschaltet → Farbe in der UI geändert → Lampe geht wieder an. | `/set_config` ignoriert `brightnessMode` (Drehknopf ist Master) bzw. fehlende Felder behalten aktuellen Wert. |
| K9 | **Config-Änderung stoppt laufenden Wecker.** `setAlarms()` → `resetAlarms()` beendet aktiven Alarm und löscht „heute schon ausgelöst“. [sicher] | `alarm.cpp:38`, `main.cpp:185` | Farbe ändern während des Weckens beendet es; Alarm kann in derselben Minute erneut feuern. | Nur Alarmliste übernehmen; aktiven Alarm/Auslösetage behalten (außer der aktive Alarm wurde gelöscht/geändert). |
| K10 | **RDS-Uhrzeit um 1–2 h falsch.** RDS-CT überträgt UTC; der Code zieht den Ortszeit-Offset zusätzlich ab. Keine Prüfung der RDS-Blockfehler, ein einzelnes Paket reicht. [sicher] | `rds_clock.cpp:116` | Falsche Uhrzeit → Wecker zur falschen Zeit. | Offset nicht abziehen; Blockfehler-Bits prüfen; zwei aufeinanderfolgende plausible CT-Pakete verlangen. |
| K11 | **TSF-/RDS-/RTC-Schalter starten WLAN neu** (Review 17.11. #3). Laufen über `STATE_CHANGE_SYSTEM_CONFIG`. [sicher] | `route_handlers.cpp:380/408/431` | Handy fliegt aus dem Hotspot, grüner Alert. | Eigene `StateChangeType`s ohne WLAN-Neustart. |
| K12 | **Verzögertes Speichern geht vor Deep Sleep verloren.** `saveFullConfig(..., true)` wartet 5 s; Deep-Sleep-Check kann vorher zuschlagen. [sicher] | `store.cpp:39`, `sleep_manager.cpp` | Letzte Helligkeits-/Config-Änderung nach Aufwachen weg. | `flushPendingSave()` vor `enterDeepSleep()` und vor jedem `ESP.restart()`. |
| K13 | **Wecker kann verpasst werden.** Auslösung nur bei exakt passender Minute; Drift-Konstante 20 ppm unrealistisch für den internen RC-Oszillator. [sicher] | `alarm.cpp:100`, `sleep_manager.cpp:16` | Wacht der ESP verspätet auf oder springt die Uhr (NTP/TSF), fällt der Alarm aus. | Auslösen, wenn Alarmzeit innerhalb der letzten N Minuten lag und heute noch nicht ausgelöst; realistischen Drift-Wert (z. B. 1–2 %) bzw. abhängig von `rtcCrystalAvailable`. |
| K14 | **Deep-Sleep-Wakeup per Taster weckt sofort.** Taster wird mit `INPUT_PULLUP` gelesen (gedrückt = LOW), für den Schlaf wird aber **Pulldown** gesetzt und auf LOW geweckt. [prüfen] | `sleep_manager.cpp:116–118` | Ohne externen Pullup am Modul: Pin ist dauerhaft LOW → sofortiges Aufwachen, Lampe geht an. | Pullup statt Pulldown im Schlaf; am Gerät testen. |

### B. Sicherheit (aus Review 17.11., weiterhin offen, plus neu)

| ID | Problem | Fix |
|----|---------|-----|
| S1 | Offener Hotspot ohne Passwort, jede/r kann Einstellungen ändern / Reset auslösen. | Default-Passwort pro Gerät (z. B. aus MAC, auf Aufkleber). |
| S2 | Kein CSRF-Schutz (kein Check von `Content-Type`/`Origin`). | Nur `application/json` annehmen, `Host`/`Origin` prüfen. |
| S3 | POST-Body unbegrenzt; `_tempObject` per `new String` angelegt, Bibliothek gibt mit `free()` frei → Leak/UB bei Abbruch. | Limit (z. B. 4 KB), `malloc`-Puffer. |
| S4 | Passwörter im Klartext im Serial-Log (`Received /set_system_config body: …`). | Body nicht loggen. |
| S5 | Factory Reset per Boot-Taste schon nach 1 s. | 5–10 s halten, mit LED-Rückmeldung. |
| S6 | Firmware-Update mit geänderter Struct-Größe löscht **alle** Einstellungen inkl. WLAN. | Versionsfeld + Migration. |
| S7 | OTA-Route ist auskommentiert, UI bietet Upload trotzdem an und meldet **fälschlich „Erfolg“** (JSON-Fehler landet im `catch` → Erfolgsmeldung). | Entweder OTA sauber (mit Größen-/Fehlerprüfung, Abbruch nach `begin()`-Fehler) aktivieren oder UI-Teil entfernen. |
| S8 | AP-Passwort kann nicht mehr entfernt werden (leeres Feld = „unverändert“). | Explizites Feld/Checkbox „Passwort entfernen“. |

### C. Mittel / klein

| ID | Problem | Ort |
|----|---------|-----|
| M1 | `/set_config` ohne Wertebereichsprüfung (Dauer 0, Helligkeit > 7, Tag > 7, Stunde > 23, colorMode > 3). | `json_utils.cpp:39ff` |
| M2 | Event-Log: `msg` nicht JSON-escaped (`"` in SSID zerstört JSON); 18-KB-String wird im Web-Task gebaut. | `event_log.cpp:47ff` |
| M3 | `lastDateResult` wird nie gesetzt → UI zeigt immer „connection_failed“. | `types.h:111`, `route_handlers.cpp:170` |
| M4 | Jeder Timer-Wakeup (alle ≤ 5 h, vor Weckern) startet den sichtbaren Hotspot für 1 min. | `main.cpp:109` |
| M5 | Timer-Wakeup: LEDs blitzen kurz auf (Helligkeit wird vor SLEEP-Übergang gesetzt). | `main.cpp:132` |
| M6 | `initRtcCrystal()` schreibt bei jedem Boot SystemSettings in den Flash. | `rtc_crystal.cpp:72` |
| M7 | Weihnachten in der UI: `month === 12` (Dezember ist 11). | `Main.svelte:18` |
| M8 | Auth-Reste: `/get_auth_status`, `webPassword`/`webPasswordSet`. | `Main.svelte:77`, `systemStore.js` |
| M9 | RDS-Status-Intervall: bei jedem Einschalten ein neues `setInterval` (Leak), beim Laden mit aktivem RDS keines. | `Settings.svelte:156` |
| M10 | `build.sh upload`: `$PIO` nicht definiert; `clean` löscht `*.gz` statt `*.gzip` und ruft `pio` ohne Pfad; Größenanzeige sucht `*.gz`. | `build.sh` |
| M11 | Vite-Dev-Proxy zeigt auf `192.168.4.1` (AP hat `4.3.2.1`) und kennt nur 4 Endpunkte. | `vite.config.js` |
| M12 | `#define DEBUG` zusätzlich zu `-DDEBUG` (Redefinition); `while(!Serial)` bei UART sinnlos + 500 ms Boot-Verzögerung. | `main.cpp:30`, `debug_utils.cpp:5` |
| M13 | Route `/app.css` liefert nur noch 404. | `route_handlers.cpp:337` |
| M14 | UI-Eingaben Dauer erlauben 0 / leere Werte. | `Main.svelte:30ff` |

---

## Teil 2 — Optimierungen und Vereinfachungen (nach Teil 1)

1. **Befehls-Queue** Web → `loop()` (Grundlage für K7/K11); `onStateUpdatedFromWifi` wird schlank.
2. **Flag-Endpunkte zusammenlegen**: `get/set_warnings`, `deep_sleep`, `tsf_fallback`, `rds_clock`, `rtc_crystal` → Felder in `/get_system_config` bzw. ein `/set_option`.
3. **`updateLed()`**: Kette von `if`s → ein `switch` über den neuen Zustand.
4. **Tote Dateien/Code entfernen**: `svelte-ui/src/components/SystemSettings.svelte` (Duplikat von `Settings.svelte`, nirgends importiert), `bak/`, `.DS_Store`, `include/KY040.h`, `include/preferences_utils.h`, ggf. `include/rda5807m.h`, `getTimeoutState`, `hasActiveAlarms`, `getAlarms`, `setOnStateChangedCallback`, `WiFiTestResult`/`WiFiStatus`-Reste, `lastNormalState` (falls nicht für K6 genutzt). Entscheidung: `sunriseFade`/`getAlarmColor` **nutzen** (Sonnenaufgangs-Wecker) oder löschen.
5. **`startWifi()`**: Hotspot-Setup-Duplikate zusammenführen.
6. **Event-Log** mit ArduinoJson / Streaming-Response statt 18-KB-String.
7. **Log-Lautstärke** senken (z. B. „Lamp state check“ alle 5 s, Helligkeits-Logs).
8. **UI**: gemeinsamer `api.js`-Fetch-Helfer mit Fehlerbehandlung; Teil-Updates statt kompletter Config; Debounce beim Posten; einheitlich Deutsch; Mock-Modus entweder vervollständigen oder entfernen.
9. **Build**: `build.sh`/`deploy.sh` zusammenführen; rechnerspezifische `upload_port`/`monitor_port` in `platformio_override.ini` (gitignored) auslagern; Bibliotheksversionen exakt pinnen.
10. **ArduinoJson 7**: `containsKey` → `is<bool>()`.

### Status Teil 2 (19.11.2026) — erledigt, je ein Commit

- Pins zentral in `include/pins.h`; Zugangsdaten in `include/secrets.h`, Ports in `platformio_override.ini`
- Tote Dateien/Funktionen entfernt (`bak/`, `KY040.h`, `preferences_utils.h`, `rda5807m.h`, …)
- `updateLed()` → `applyStateEntry()` mit `switch`
- `startWifi()`: `startCaptivePortal()` statt doppeltem Code
- Periodische Serial-Ausgaben reduziert
- TSF-Callback-Barriere, RDS-Wiederholung (1 h / 24 h), RDS-Modul-Erkennung per I2C
- Schalter-Endpunkte → `POST /set_option`; Handler dateiintern
- Bibliotheksversionen exakt gepinnt
- Nicht umgesetzt: Mock-Modus vervollständigen (reicht für UI-Tests), Settings-Migration (S6, TODO in README)

## Teil 3 — Dokumentation säubern (nach Teil 2)

Vorhanden: `README` (ohne Endung), `SVELTE_INTEGRATION.md`, `svelte-ui/DEPLOYMENT.md`, `svelte-ui/MOCK_MODE.md`,
`doc/architektur.md`, `zustaende.md`, `controls.md`, `commands.md` (ungetrackt), `pins.md`, `wifi.md`, `wlan.md`,
`uhrzeit-fallback.md`, `review.md`, `review-2026-11-17.md`, `.github/copilot-instructions.md`, `include/README`, `lib/README`.

Ziel-Struktur:
- `README.md` — Überblick, Hardware, Schnellstart (Build/Deploy), Links.
- `doc/architektur.md` — Module, Tasks/Threads, Datenfluss (inkl. neuer Befehls-Queue).
- `doc/zustaende.md` — Lampenzustände + Prioritäten (an K3–K6 angepasst).
- `doc/bedienung.md` — aus `controls.md` + `commands.md`.
- `doc/hardware.md` — aus `pins.md` (+ Bilder).
- `doc/wlan-und-zeit.md` — aus `wifi.md`, `wlan.md`, `uhrzeit-fallback.md`.
- `doc/build-und-deploy.md` — aus `SVELTE_INTEGRATION.md`, `DEPLOYMENT.md`, `MOCK_MODE.md`.
- Reviews (`review.md`, `review-2026-11-17.md`, dieser Plan) nach Abschluss löschen bzw. in `CHANGELOG.md` überführen.
- PlatformIO-Standard-READMEs (`include/README`, `lib/README`) löschen; `copilot-instructions.md` prüfen/aktualisieren.
- Alle Datei-Header und Funktionskommentare auf Deutsch und aktuell.

---

## Umsetzung Teil 1 (nach Freigabe)

Günstige Agenten (Sonnet/Haiku), aufgeteilt nach Dateien, damit sich nichts überschneidet:

| Paket | Inhalt | Dateien |
|-------|--------|---------|
| P1 Zustände & Bedienung | K3, K4, K5, K6, K9, K13, M5 | `lamp_state.cpp`, `button.cpp`, `alarm.cpp`, `led.*` |
| P2 Sleep & Speichern | K12, K14, M6, M4 | `sleep_manager.cpp`, `store.*`, `rtc_crystal.cpp`, `main.cpp` (setup) |
| P3 WLAN & Zeit | K1, K10, M3 | `wifi_controller.cpp`, `rds_clock.cpp`, `tsf_clock.cpp`, `types.h` |
| P4 Web-API & Sicherheit | K2, K7, K8, K11, S2–S5, S7, S8, M1, M2, M13 | `route_handlers.cpp`, `captive_portal.cpp`, `json_utils.cpp`, `event_log.cpp`, `main.cpp` (Callback) |
| P5 UI & Build | K2 (UI), K8 (UI), S7 (UI), M7–M11, M14 | `svelte-ui/**`, `build.sh`, `vite.config.js` |

S1 (Default-Passwort) und S6 (Settings-Migration) brauchen eine Entscheidung von dir (siehe unten).
Danach: Firmware- und UI-Build, Review aller Diffs durch mich.

### Status Teil 1 (19.11.2026)

Umgesetzt (P1–P5), Firmware und UI bauen ohne Warnungen, UI im Mock-Modus geprüft. **Nicht am Gerät getestet.**

- Erledigt: K1–K13, S2, S3, S4, S7 (OTA entfernt), S8, M1–M14. Sonnenaufgang-Code gelöscht.
- K14 umgesetzt, **am Gerät prüfen**: Pullup + Pad-Hold für GPIO 3/4 im Deep Sleep; Drehen weckt nur,
  wenn CLK vor dem Schlaf HIGH ist.
- S1 erledigt: Standard-Hotspot-Passwort `Erdmaennchen` (ASCII, in `README.md` dokumentiert).
- S6 bewusst offen, als TODO in `README.md` dokumentiert.
- S5 verworfen: Werksreset per Boot-Taste bleibt wie er ist (1 s halten).
- Beim Review zusätzlich behoben:
  - UI zeigte Fehler als rohes JSON → `api.js` liest `message`.
  - Speichern der WLAN-Einstellungen hat Warn-/Sleep-Schalter mit altem Stand überschrieben → werden nicht mehr mitgesendet.
  - Einstellungsseite zeigte gar keine Meldungen (fehlende `MessageAlert`, Altlast aus dem Seiten-Umbau).
  - Vite-Proxy leitete `pico.min.css` an die Lampe → Dev-Seite blieb ohne Lampe leer; jetzt nur API-Pfade.
  - `-Wvolatile`-Warnung in `tsf_clock.cpp`.
- Verdachtsfälle für Teil 2 (aus P3): Promiscuous-Callback ohne Sperre gegen `processSnapshot`;
  RDS wird nach DONE/NO_SIGNAL nicht neu gestartet; `chipInit()`-Kommentar passt nicht zu den Bits;
  I2C-Fehler in `readReg` unbehandelt.

### Gerätetest (19.11.2026, Firmware aufgespielt)

Bestätigt: Boot, WLAN/NTP-Sync, TSF-Snapshot, Deep Sleep, Wakeup per Taster (K14), Standard-Passwort
nach Werksreset, Web-Werksreset.

Beim Test zusätzlich behoben:
- **Licht beim Aufwecken kam erst nach ~3,4 s** (nach WLAN-Kanal-Scan/AP-Start). Jetzt: Aufwachgrund wird
  direkt nach dem Laden der Config ausgewertet und die LEDs sofort gesetzt (gemessen 59 ms). Gewählter
  WLAN-Kanal liegt im RTC-Speicher (`RTC_DATA_ATTR`) → nach Deep Sleep kein Kanal-Scan.
- **Drehknopf verschluckte einzelne Rastungen.** Ursachen in AiEsp32RotaryEncoder: aktive Beschleunigung
  (Extra-Flanken), Division mit Rundung Richtung 0, interne Pulldowns gegen die Modul-Pullups. Jetzt:
  Bibliothek zählt Rohflanken (Schrittweite 1, Pullups, Beschleunigung aus); `handleRotation()` wertet nur
  in der Rastung (beide Pins auf gelerntem Ruhepegel) aus, rundet auf ganze Rastungen und gleicht neu ab.
  Am Gerät bestätigt.

### Gerätetest-Checkliste
1. Hotspot erscheint, UI lädt, Einstellungen speichern (inkl. Passwort < 8 → Fehlermeldung).
2. Drehen: Helligkeit, aus Gute-Nacht und aus Sleep heraus.
3. Langer Druck im Sleep → gelber Blitz, Lampe bleibt aus, Hotspot startet.
4. Deep Sleep: Lampe aus → schläft nach ~1 min ohne WLAN-Client; Taster weckt, kein Sofort-Wakeup.
5. Wecker: Farbe in der UI ändern während Wecker läuft → Wecker läuft weiter.
6. Event-Log nach NTP-Sync: kein „NTP sync lost“ nach einer Minute.
