# LisasLamp ESP32 — Code Review & Umsetzungsplan

_Stand: November 2026_

---

## Befunde

### A — Celebration-Bug (Party-Modus startet ungewollt) 🔴

**Ursache:** `checkCelebrationLogic()` prüft nicht ob die Systemzeit gültig ist.

Wenn kein NTP-Sync stattgefunden hat, ist `time()` = Unix-Epoch 0 = **1. Januar 1970**.
Dieser Tag (`{1, 1}`) steht in `celebrationDates[]`. Sobald `tm_hour >= 7` → Celebration startet.
Da der ESP32 nach dem Boot ohne Internet einige "Stunden" in der Epoch läuft, kann dies unkontrolliert einsetzen.

**Fix:** Vor `isCelebrationDate()` prüfen: `ptm->tm_year >= (2024 - 1900)`.

**Datei:** `src/celebration.cpp:38`

---

### B — WiFi STA-Reconnect verliert Verbindung dauerhaft 🔴

**Ursache:** Mehrere Bugs im Reconnect-Flow.

1. Nach 3 fehlgeschlagenen STA-Versuchen wird auf `WIFI_MODE_AP` gewechselt (`wifi_controller.cpp:225`).
2. `tryReconnectSta()` prüft `WiFi.getMode() != WIFI_MODE_APSTA` — da der Mode jetzt `AP` ist, wird **nie** wieder STA versucht.
3. STA ist dauerhaft verloren bis zum nächsten manuellen `startWifi()` (Long-Press) oder Neustart.

**Zusätzlich:** `server.begin()` / `server.end()` / `server.begin()` erneut — der `AsyncWebServer` ist darauf nicht ausgelegt und kann in einen instabilen Zustand geraten. Selbes Problem mit `dnsServer.start()` / `dnsServer.stop()`.

**Fix:**
- `tryReconnectSta()`: Zusätzlich auf `WIFI_MODE_AP` prüfen und dann `WiFi.mode(WIFI_MODE_APSTA)` + `WiFi.begin()` aufrufen.
- `server.begin()` nur einmal im Init aufrufen; bei Stop/Start nur WiFi togglen, Server laufen lassen.

**Datei:** `src/wifi_controller.cpp:107–118`, `121–143`, `145–159`

---

### C — Zeitdrift ohne Internet 🟠

**Ursache:** Hardware + fehlende Persistenz.

- ESP32-C3 Super Mini hat keinen externen 32.768 kHz-Quarz. Der interne RC-Oszillator driftet **1–2% = 864–1728 Sekunden/Tag** je nach Temperatur.
- Nach einem Neustart ohne Internet beginnt `time()` bei Epoch 0 (kein gespeicherter Timestamp).
- `isTimeSyncedWithNTP()` gibt `false` während eines laufenden `sntp_restart()`, was kurzzeitig Fehlalarme auslösen kann.

**Fix:**
- Letzten validen Unix-Timestamp in NVS/Preferences speichern (z.B. jede Stunde nach erfolgtem Sync).
- Nach Neustart wiederherstellen: `settimeofday()` mit gespeichertem Wert — besser als Epoch 0.
- Optional: Warnhinweis im Status wenn gespeicherte Zeit > 24h alt und kein NTP.

**Datei:** `src/wifi_controller.cpp:185–191`, neuer Code in `src/store.cpp`

---

### D — API-Bereinigung 🟡

- **`MAX_ALARMS` doppelt definiert**: in `types.h:33` und `alarm.cpp:7` — bei Änderung nur einer Stelle entsteht ein stiller Bug.
- **`serialPrint`-Bool-Bug**: `serialPrint("send status " + status.clockSynced)` in `route_handlers.cpp:203` — bool + String-Operator gibt `0`/`1`, nicht `"true"`/`"false"`.
- **HTTP_ANY statt HTTP_GET**: Route `/` in `route_handlers.cpp:226` sollte `HTTP_GET` sein.
- **Inkonsistente Fehlerformate**: Manche Endpoints antworten `text/plain`, andere `application/json`. Sollte vereinheitlicht werden.

---

### E — Code-Cleanup 🟡

- **Auskommentierter Code**: ~15 Blöcke in `main.cpp`, `// ...existing code...` in `alarm.cpp:12` (AI-Artefakt), tote Variablen `savePending`/`lastSavedTime` in `store.cpp`.
- **Doppeltes Include**: `#include <esp_task_wdt.h>` zweimal in `main.cpp` (Zeilen 14 + 491).
- **`bak/` Verzeichnis**: Alte Dateien (`led.cpp`, `state.cpp`) noch im Repo.
- **Unbenutzte Symbole**:
  - `RANGE`-Struct in `types.h` — nirgendwo genutzt
  - `StateChangeType::STATE_CHANGE_ALARMS`, `STATE_CHANGE_CUSTOM_COLOR` — nicht mehr genutzt
  - `enterDeepSleep()` — implementiert aber nie aufgerufen
  - `lastNormalState` in `main.cpp` — bleibt immer `LAMP_STATE_DEFAULT`
  - `isWifiActiveAndNotUsed()` — definiert, in `main.cpp` nicht genutzt
- **`preferences_utils.h`** wird includiert, zugehörige `.cpp` fehlt (oder liegt in `bak/`).

---

### F — OTA Online-Update 🟢

Noch nicht implementiert.

**Machbarer Ansatz:**
1. Neuer POST-Endpoint `/ota` in `route_handlers.cpp`
2. Empfang der Firmware-Binary via HTTP Multipart
3. `Update.begin()` / `Update.write()` / `Update.end()` aus `<Update.h>`
4. Bei Erfolg `ESP.restart()`
5. Svelte-UI: Datei-Upload-Komponente in SystemSettings

PlatformIO unterstützt OTA nativ (`upload_protocol = espota`), braucht aber `ArduinoOTA` laufend im Hintergrund — erhöht RAM-Verbrauch. Der HTTP-Upload-Weg ist leichtgewichtiger.

---

## Umsetzungsplan

| Schritt | Thema | Aufwand | Priorität |
|---------|-------|---------|-----------|
| A | Celebration-Bug (Jahr-Check) | ~5 Zeilen | 🔴 sofort |
| B | WiFi STA-Reconnect reparieren + Server nicht neu starten | ~30 Zeilen | 🔴 hoch |
| C | Zeitdrift — NVS-Timestamp-Backup + Restore | ~50 Zeilen | 🟠 hoch |
| D | API-Bereinigung | ~20 Zeilen | 🟡 mittel |
| E | Code-Cleanup | ~diffuse Änderungen | 🟡 mittel |
| F | OTA Online-Update | ~150 Zeilen + UI | 🟢 feature |

**Empfohlene Reihenfolge:** A → B → C → D+E zusammen → F
