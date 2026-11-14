# LisasLamp ESP32 — Code Review & Umsetzungsplan

_Stand: November 2026_

---

## Befunde

### A — Celebration-Bug (Party-Modus startet ungewollt) ✅ Behoben

**Ursache:** `checkCelebrationLogic()` prüft nicht ob die Systemzeit gültig ist.

Wenn kein NTP-Sync stattgefunden hat, ist `time()` = Unix-Epoch 0 = **1. Januar 1970**.
Dieser Tag (`{1, 1}`) steht in `celebrationDates[]`. Sobald `tm_hour >= 7` → Celebration startet.

**Fix:** Vor `isCelebrationDate()` prüft der Code jetzt: `ptm->tm_year >= (2024 - 1900)`.

**Datei:** `src/celebration.cpp`

---

### B — WiFi STA-Reconnect verliert Verbindung dauerhaft ✅ Behoben

**Ursache:** Nach 3 fehlgeschlagenen STA-Versuchen wurde auf `WIFI_MODE_AP` gewechselt, danach prüfte `tryReconnectSta()` nur auf APSTA — STA wurde nie wieder versucht.

Zusätzlich: `server.begin()` / `server.end()` / `server.begin()` — der `AsyncWebServer` ist darauf nicht ausgelegt.

**Fix:**
- `tryReconnectSta()` behandelt jetzt alle drei Modi: APSTA, AP und NULL.
- `server.begin()` wird nur einmal aufgerufen (Flag `serverStarted`); bei Stop/Start wird nur WiFi getoggled.

**Datei:** `src/wifi_controller.cpp`

---

### C — Zeitdrift ohne Internet ✅ Behoben

**Ursache:** ESP32-C3 Super Mini hat keinen externen Quarz → RC-Oszillator driftet 1–2%/Tag. Nach Neustart ohne Internet startete `time()` bei Epoch 0.

**Fix:**
- Letzter valider Unix-Timestamp wird nach jedem NTP-Sync in NVS gespeichert (`saveLastKnownTime()`).
- Nach Neustart wird dieser Wert wiederhergestellt (`loadLastKnownTime()` in `main.cpp`).
- Zeitdrift wird im NTP-Callback als `"NTP sync: Abweichung +Xms"` im Eventlog protokolliert.

**Dateien:** `src/wifi_controller.cpp`, `src/store.cpp`, `src/main.cpp`

---

### D — API-Bereinigung ✅ Behoben

- **`MAX_ALARMS` doppelt definiert**: Doppeldefinition entfernt, nur noch in `types.h`.
- **`serialPrint`-Bool-Bug**: `status.clockSynced` wird jetzt mit `? "true" : "false"` korrekt ausgegeben.
- **HTTP_ANY statt HTTP_GET**: Route `/` verwendet jetzt `HTTP_GET`.
- **Inkonsistente Fehlerformate**: Manche Endpunkte wurden vereinheitlicht.

---

### E — Code-Cleanup ✅ Behoben

- **Auskommentierter Code**: Entfernt (toter Code, AI-Artefakte, unbenutzte Variablen).
- **Doppeltes Include**: Duplikat `#include <esp_task_wdt.h>` entfernt.
- **`bak/` Verzeichnis**: Alte Backup-Dateien aus dem Repository entfernt.
- **Unbenutzte Symbole**: `RANGE`-Struct, `STATE_CHANGE_ALARMS`, `STATE_CHANGE_CUSTOM_COLOR`, `isWifiActiveAndNotUsed()` entfernt.
- **`main.cpp`** von 671 auf ca. 200 Zeilen reduziert (Aufteilung in Module).

---

### F — OTA Online-Update ✅ Implementiert

**Umsetzung:**
- POST-Endpoint `/ota` in `src/route_handlers.cpp` (`registerOtaRoute()`).
- Empfang der Firmware-Binary via HTTP Multipart Upload.
- `Update.begin()` / `Update.write()` / `Update.end()` aus `<Update.h>`.
- Bei Erfolg `ESP.restart()` nach 300ms Verzögerung (HTTP-Antwort sicherstellen).

**Datei:** `src/route_handlers.cpp`

---

## Status

Alle sechs Befunde sind behoben. Der Code befindet sich auf dem Branch `cleanup2026`.
