// TSF-Uhrzeit-Fallback — Implementierung
// Erfasst Beacon-Frames per Promiscuous-Scan, berechnet mittels Medianfilter
// und Ausreißerelimination einen Unix-Zeitstempel und setzt damit die Systemzeit.
// Neue APs werden per Quality-Score (RSSI + Laufzeit) gegen schwächere Einträge ausgetauscht.
// Jeder Beacon bekommt beim Empfang einen monotonen esp_timer-Zeitstempel, damit die Unix-Zeit
// exakt zum Empfangszeitpunkt (und nicht zum Scan-Start oder -Ende) berechnet wird.
#include "tsf_clock.h"
#include "store.h"
#include "debug_utils.h"
#include "system_utils.h"
#include "event_log.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <sys/time.h>
#include <algorithm>
#include <atomic>
#include <ArduinoJson.h>

#define MIN_VALID_EPOCH_US (1577836800ULL * 1000000ULL) // 2020-01-01: alles davor gilt als "Uhrzeit unbekannt"
#define MAX_DEVIATION_US 30000000ULL // Schätzungen weiter als 30 s vom Median entfernt sind Ausreißer

static const SystemSettings* g_settings = nullptr;
static WiFiTestTracker* g_tracker = nullptr;

static TsfEntry g_storedEntries[MAX_TSF_ENTRIES];
static uint8_t g_storedCount = 0;

struct CapturedBeacon {
    uint8_t bssid[6];
    uint64_t tsf_us;
    int64_t monoUs; // esp_timer-Zeit beim Empfang des Beacons
    int8_t rssi;
};

#define MAX_CAPTURE 20
static CapturedBeacon g_capturedBeacons[MAX_CAPTURE];
static volatile uint8_t g_capturedBeaconCount = 0;

static unsigned long g_captureStartMs = 0;
static uint64_t g_captureUnixUs = 0; // Unix-Zeit beim Scan-Start ...
static int64_t g_captureMonoUs = 0;  // ... und die zugehörige esp_timer-Zeit
static volatile bool g_captureRunning = false;

static volatile bool g_ntpSyncedTrigger = false;
static bool g_snapshotScheduled = false;
static bool g_fallbackAttempted = false;
static bool g_fallbackScheduled = false;

static unsigned long g_lastSnapshotMs = 0;
static const char* g_lastCalcResult = "none"; // nur String-Literale zuweisen (wird aus dem Web-Task gelesen)
static uint8_t g_lastCalcAps = 0;
static uint32_t g_lastCalcDeviationMs = 0;

// Läuft im WiFi-Task (nicht in einer ISR) für jedes empfangene Management-Frame; filtert auf
// Beacon-Frames und speichert pro AP (BSSID) genau einen TSF-Wert, RSSI und Empfangszeitpunkt
static void IRAM_ATTR beaconRxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if(!g_captureRunning)
        return;
    if(type != WIFI_PKT_MGMT)
        return;
    if(g_capturedBeaconCount >= MAX_CAPTURE)
        return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if(len < 36)
        return;
    if(payload[0] != 0x80 || payload[1] != 0x00)
        return;

    uint8_t bssid[6];
    memcpy(bssid, payload + 16, 6);

    for(uint8_t i = 0; i < g_capturedBeaconCount; i++) {
        if(memcmp(g_capturedBeacons[i].bssid, bssid, 6) == 0)
            return;
    }

    uint64_t tsf;
    memcpy(&tsf, payload + 24, 8);

    uint8_t idx = g_capturedBeaconCount;
    memcpy(g_capturedBeacons[idx].bssid, bssid, 6);
    g_capturedBeacons[idx].tsf_us = tsf;
    g_capturedBeacons[idx].monoUs = esp_timer_get_time();
    g_capturedBeacons[idx].rssi = pkt->rx_ctrl.rssi;
    // Barriere: der Eintrag muss vollständig geschrieben sein, bevor der Zähler ihn sichtbar macht
    // (der Hauptthread liest nur Einträge < g_capturedBeaconCount)
    std::atomic_thread_fence(std::memory_order_release);
    g_capturedBeaconCount = idx + 1; // kein ++ auf volatile (C++20 deprecated)
}

// Aktiviert den Promiscuous-Modus und startet die Beacon-Erfassung;
// speichert Unix- und Monotonzeit als gemeinsame Referenz für spätere Berechnungen.
// Gibt false zurück wenn der Promiscuous-Modus nicht gestartet werden konnte.
static bool startCapture() {
    g_capturedBeaconCount = 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    g_captureUnixUs = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    g_captureMonoUs = esp_timer_get_time();
    g_captureStartMs = millis();

    wifi_promiscuous_filter_t filter = {};
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(beaconRxCallback);
    g_captureRunning = true;
    if(esp_wifi_set_promiscuous(true) != ESP_OK) {
        g_captureRunning = false;
        esp_wifi_set_promiscuous_rx_cb(nullptr);
        return false;
    }
    return true;
}

// Beendet den Promiscuous-Modus und deregistriert den Beacon-Callback
static void stopCapture() {
    g_captureRunning = false; // zuerst: der Callback ignoriert danach alle Pakete
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
}

// Berechnet den Quality-Score eines Eintrags:
// score = (rssi + 100) + min(tsf_tage, 365)
// Lange Laufzeit (stabiler AP) wird stark gewichtet, RSSI ist sekundär.
static int calcScore(int8_t rssi, uint64_t tsf_us) {
    int rssi_score = (int)rssi + 100;
    if(rssi_score < 0)
        rssi_score = 0;
    uint64_t days = tsf_us / (86400ULL * 1000000ULL);
    if(days > 365)
        days = 365;
    return rssi_score + (int)days;
}

// Verarbeitet einen NTP-Snapshot: mergt neue APs per Quality-Score in die gespeicherte Liste.
// Bekannte APs werden aktualisiert (frischerer Messzeitpunkt = besserer Score).
// Unbekannte APs verdrängen nur den schwächsten Eintrag wenn ihr Score höher ist.
// Nicht gescannte APs bleiben unberührt in der Liste.
static void processSnapshot() {
    g_snapshotScheduled = false;
    uint8_t count = g_capturedBeaconCount;
    std::atomic_thread_fence(std::memory_order_acquire); // Gegenstück zur Barriere im Callback

    if(count == 0) {
        serialPrint("TSF: Snapshot: keine APs");
        return;
    }
    if(g_captureUnixUs < MIN_VALID_EPOCH_US) {
        // Ohne gültige Systemzeit wären die gespeicherten Referenzen wertlos
        serialPrint("TSF: snapshot discarded, time invalid");
        logEvent("TSF: snapshot discarded, time invalid");
        return;
    }

    uint8_t newAdded = 0;
    uint8_t updated = 0;

    for(uint8_t n = 0; n < count; n++) {
        const CapturedBeacon& b = g_capturedBeacons[n];
        TsfEntry newEntry;
        memcpy(newEntry.bssid, b.bssid, 6);
        newEntry.tsf_us = b.tsf_us;
        // Unix-Zeit zum Empfangszeitpunkt dieses Beacons (nicht zum Scan-Start)
        newEntry.unix_us = g_captureUnixUs + (uint64_t)(b.monoUs - g_captureMonoUs);
        newEntry.rssi = b.rssi;
        int newScore = calcScore(newEntry.rssi, newEntry.tsf_us);

        // Bereits bekannter AP: Eintrag mit frischen Messwerten überschreiben
        bool found = false;
        for(uint8_t s = 0; s < g_storedCount; s++) {
            if(memcmp(g_storedEntries[s].bssid, newEntry.bssid, 6) == 0) {
                g_storedEntries[s] = newEntry;
                updated++;
                found = true;
                break;
            }
        }
        if(found)
            continue;

        if(g_storedCount < MAX_TSF_ENTRIES) {
            g_storedEntries[g_storedCount++] = newEntry;
            newAdded++;
            continue;
        }

        // Liste voll: schwächsten Eintrag suchen und ggf. ersetzen
        uint8_t worstIdx = 0;
        int worstScore = calcScore(g_storedEntries[0].rssi, g_storedEntries[0].tsf_us);
        for(uint8_t s = 1; s < g_storedCount; s++) {
            int sc = calcScore(g_storedEntries[s].rssi, g_storedEntries[s].tsf_us);
            if(sc < worstScore) {
                worstScore = sc;
                worstIdx = s;
            }
        }
        if(newScore > worstScore) {
            g_storedEntries[worstIdx] = newEntry;
            newAdded++;
        }
    }

    saveTsfEntries(g_storedEntries, g_storedCount);
    g_lastSnapshotMs = millis();

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: snapshot %d new, %d replaced, %d total", newAdded, updated, g_storedCount);
    logEvent(msg);
    serialPrint(msg);
}

// Eintrag für "zu wenig brauchbare APs" ins Ergebnis und ins Event-Log
static void reportInsufficient(uint8_t aps) {
    g_lastCalcResult = "insufficient";
    g_lastCalcAps = aps;
    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: too few APs (%d)", aps);
    logEvent(msg);
}

struct Estimate {
    uint64_t unixUs;  // geschätzte Unix-Zeit zum Zeitpunkt nowMonoUs
    uint8_t stored;   // Index in g_storedEntries
    uint8_t captured; // Index in g_capturedBeacons
};

// Berechnet die aktuelle Unix-Zeit aus TSF-Differenzen zwischen gespeichertem Snapshot
// und neuen Beacon-Messungen. Ausreißer (> 30 s vom Median) werden verworfen, dann wird
// der Median der übrigen Schätzungen genommen. Einträge mit zurückgesetztem TSF-Zähler
// und Ausreißer werden neu basiert.
static void processFallback() {
    g_fallbackScheduled = false;
    g_fallbackAttempted = true;
    const uint8_t newCount = g_capturedBeaconCount;
    std::atomic_thread_fence(std::memory_order_acquire); // Gegenstück zur Barriere im Callback

    if(newCount == 0 || g_storedCount == 0) {
        g_lastCalcResult = "insufficient";
        g_lastCalcAps = 0;
        logEvent("TSF: fallback has no data");
        return;
    }

    // Alle Schätzungen beziehen sich auf diesen Zeitpunkt ("jetzt")
    const int64_t nowMonoUs = esp_timer_get_time();

    Estimate est[MAX_TSF_ENTRIES];
    uint8_t estCount = 0;
    uint8_t rebaseStored[MAX_TSF_ENTRIES];
    uint8_t rebaseCaptured[MAX_TSF_ENTRIES];
    uint8_t rebaseCount = 0;

    // Jeder gespeicherte AP kommt höchstens einmal vor (Schätzung ODER Rebase)
    for(uint8_t s = 0; s < g_storedCount; s++) {
        for(uint8_t n = 0; n < newCount; n++) {
            if(memcmp(g_storedEntries[s].bssid, g_capturedBeacons[n].bssid, 6) != 0)
                continue;

            const CapturedBeacon& b = g_capturedBeacons[n];
            const TsfEntry& e = g_storedEntries[s];

            if(b.tsf_us < e.tsf_us) {
                // TSF-Zähler zurückgesetzt (AP neu gestartet) → neu basieren
                rebaseStored[rebaseCount] = s;
                rebaseCaptured[rebaseCount] = n;
                rebaseCount++;
            } else {
                // Zeit beim Empfang = Snapshot-Zeit + TSF-Differenz; plus die Zeit seit dem Empfang
                est[estCount++] = {e.unix_us + (b.tsf_us - e.tsf_us) + (uint64_t)(nowMonoUs - b.monoUs), s, n};
            }
            break;
        }
    }

    if(estCount < 3) {
        reportInsufficient(estCount);
        return;
    }

    std::sort(est, est + estCount, [](const Estimate& a, const Estimate& b) { return a.unixUs < b.unixUs; });

    // Ausreißer relativ zum Median aussortieren und neu basieren
    const uint64_t firstMedian = est[estCount / 2].unixUs;
    Estimate kept[MAX_TSF_ENTRIES];
    uint8_t keptCount = 0;
    for(uint8_t i = 0; i < estCount; i++) {
        uint64_t dev = est[i].unixUs > firstMedian ? est[i].unixUs - firstMedian : firstMedian - est[i].unixUs;
        if(dev > MAX_DEVIATION_US) {
            rebaseStored[rebaseCount] = est[i].stored;
            rebaseCaptured[rebaseCount] = est[i].captured;
            rebaseCount++;
        } else {
            kept[keptCount++] = est[i];
        }
    }

    if(keptCount < 3) {
        reportInsufficient(keptCount);
        return;
    }

    const uint64_t medianNowUs = kept[keptCount / 2].unixUs; // kept ist weiterhin sortiert
    if(medianNowUs < MIN_VALID_EPOCH_US) {
        g_lastCalcResult = "invalid";
        g_lastCalcAps = keptCount;
        logEvent("TSF: invalid reference time");
        return;
    }

    uint64_t devSum = 0;
    for(uint8_t i = 0; i < keptCount; i++) {
        devSum += kept[i].unixUs > medianNowUs ? kept[i].unixUs - medianNowUs : medianNowUs - kept[i].unixUs;
    }
    uint32_t deviationMs = (uint32_t)(devSum / keptCount / 1000ULL);

    // Die Rechenzeit seit nowMonoUs (wenige µs bis ms) noch dazurechnen
    const uint64_t setUs = medianNowUs + (uint64_t)(esp_timer_get_time() - nowMonoUs);
    struct timeval tv;
    tv.tv_sec = (time_t)(setUs / 1000000ULL);
    tv.tv_usec = (suseconds_t)(setUs % 1000000ULL);
    settimeofday(&tv, nullptr);
    // Zeit gilt jetzt als gültig → keine "Zeit veraltet"-Warnung; clockSynced (= NTP) bleibt bewusst unverändert
    if(g_tracker != nullptr)
        g_tracker->lastSucceededTestTime = millis();

    if(rebaseCount > 0) {
        for(uint8_t i = 0; i < rebaseCount; i++) {
            const CapturedBeacon& b = g_capturedBeacons[rebaseCaptured[i]];
            TsfEntry& e = g_storedEntries[rebaseStored[i]];
            e.tsf_us = b.tsf_us;
            e.unix_us = medianNowUs - (uint64_t)(nowMonoUs - b.monoUs); // Zeit beim Empfang dieses Beacons
            e.rssi = b.rssi;                                            // aktuellerer Messwert
        }
        saveTsfEntries(g_storedEntries, g_storedCount);
        char msg[52];
        snprintf(msg, sizeof(msg), "TSF: %d entries rebased", rebaseCount);
        logEvent(msg);
    }

    g_lastCalcResult = "success";
    g_lastCalcAps = keptCount;
    g_lastCalcDeviationMs = deviationMs;

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: time set, %d APs +-%lums", keptCount, (unsigned long)deviationMs);
    logEvent(msg);
    serialPrint(msg);
}

// Initialisiert das Modul: speichert Einstellungszeiger und lädt TSF-Referenzdaten aus dem Flash
void initTsfClock(const SystemSettings* settings, WiFiTestTracker* tracker) {
    g_settings = settings;
    g_tracker = tracker;
    g_storedCount = loadTsfEntries(g_storedEntries, MAX_TSF_ENTRIES);

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: Init, %d APs geladen", g_storedCount);
    serialPrint(msg);
}

// Wird nach erfolgreicher NTP-Synchronisation aufgerufen; löst neuen Snapshot aus
void onNtpSynced() {
    if(g_settings == nullptr || !g_settings->tsfFallbackEnabled)
        return;
    g_ntpSyncedTrigger = true;
}

// Manueller Snapshot-Auslöser (z.B. per Web-API); ignoriert laufende Scans
void triggerTsfSnapshot() {
    if(g_captureRunning)
        return;
    g_ntpSyncedTrigger = true;
}

// Hauptschleife: startet Snapshot-Scans nach NTP-Sync und Fallback-Scans wenn keine Uhrzeit verfügbar
void tsfClockLoop() {
    if(g_settings == nullptr || !g_settings->tsfFallbackEnabled)
        return;

    if(g_ntpSyncedTrigger && !g_snapshotScheduled && !g_captureRunning) {
        g_ntpSyncedTrigger = false;
        g_fallbackAttempted = false;
        if(startCapture()) {
            g_snapshotScheduled = true;
            serialPrint("TSF: Starte Snapshot-Scan");
        } else {
            logEvent("TSF: scan start failed");
        }
        return;
    }

    bool staActive = WiFi.getMode() == WIFI_MODE_APSTA;
    if(!g_snapshotScheduled && !g_captureRunning && !g_fallbackAttempted && !g_fallbackScheduled && g_storedCount > 0
       && staActive && g_tracker != nullptr && !g_tracker->clockSynced) {
        static unsigned long lastFallbackCheckMs = 0;
        if(isTimeForAction(&lastFallbackCheckMs, 60 * 1000)) {
            if(startCapture()) {
                g_fallbackScheduled = true;
                serialPrint("TSF: Starte Fallback-Scan");
            } else {
                g_fallbackAttempted = true;
                logEvent("TSF: scan start failed");
            }
            return;
        }
    }

    if(g_captureRunning && millis() - g_captureStartMs >= 2000) {
        stopCapture();
        if(g_snapshotScheduled) {
            processSnapshot();
        } else if(g_fallbackScheduled) {
            processFallback();
        }
    }
}

// Gibt TSF-Modul-Status als JSON zurück: aktiviert, gespeicherte APs, letzter Snapshot,
// Berechnungsergebnis sowie Score-Bereich (min/max) der gespeicherten Einträge
String getTsfStatusJson() {
    JsonDocument doc;
    doc["enabled"] = g_settings != nullptr && g_settings->tsfFallbackEnabled;
    doc["storedAps"] = g_storedCount;
    doc["lastSnapshotMs"] = g_lastSnapshotMs > 0 ? (unsigned long)(millis() - g_lastSnapshotMs) : 0;
    doc["lastCalcResult"] = g_lastCalcResult;
    doc["lastCalcAps"] = g_lastCalcAps;
    doc["lastCalcDeviationMs"] = g_lastCalcDeviationMs;

    if(g_storedCount > 0) {
        int minSc = calcScore(g_storedEntries[0].rssi, g_storedEntries[0].tsf_us);
        int maxSc = minSc;
        for(uint8_t i = 1; i < g_storedCount; i++) {
            int sc = calcScore(g_storedEntries[i].rssi, g_storedEntries[i].tsf_us);
            if(sc < minSc)
                minSc = sc;
            if(sc > maxSc)
                maxSc = sc;
        }
        doc["minScore"] = minSc;
        doc["maxScore"] = maxSc;
    }

    String out;
    serializeJson(doc, out);
    return out;
}