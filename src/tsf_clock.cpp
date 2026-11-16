// TSF-Uhrzeit-Fallback — Implementierung
// Erfasst Beacon-Frames per Promiscuous-Scan, berechnet mittels Medianfilter
// und Ausreißerelimination einen Unix-Zeitstempel und setzt damit die Systemzeit.
// Neue APs werden per Quality-Score (RSSI + Laufzeit) gegen schwächere Einträge ausgetauscht.
#include "tsf_clock.h"
#include "store.h"
#include "debug_utils.h"
#include "system_utils.h"
#include "event_log.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <sys/time.h>
#include <algorithm>
#include <cmath>
#include <ArduinoJson.h>

static const SystemSettings* g_settings = nullptr;
static WiFiTestTracker* g_tracker = nullptr;

static TsfEntry g_storedEntries[MAX_TSF_ENTRIES];
static uint8_t g_storedCount = 0;

struct CapturedBeacon {
    uint8_t bssid[6];
    uint64_t tsf_us;
    int8_t rssi;
};

#define MAX_CAPTURE 20
static CapturedBeacon g_capturedBeacons[MAX_CAPTURE];
static volatile uint8_t g_capturedBeaconCount = 0;

static unsigned long g_captureStartMs = 0;
static uint64_t g_captureUnixUs = 0;
static bool g_captureRunning = false;

static bool g_ntpSyncedTrigger = false;
static bool g_snapshotScheduled = false;
static bool g_fallbackAttempted = false;
static bool g_fallbackScheduled = false;

static unsigned long g_lastSnapshotMs = 0;
static String g_lastCalcResult = "none";
static uint8_t g_lastCalcAps = 0;
static uint32_t g_lastCalcDeviationMs = 0;

// ISR: wird für jedes empfangene Management-Frame aufgerufen; filtert auf Beacon-Frames
// und speichert pro AP (BSSID) genau einen TSF-Wert und RSSI
static void IRAM_ATTR beaconRxCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    if (g_capturedBeaconCount >= MAX_CAPTURE) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    uint8_t* payload = pkt->payload;
    uint16_t len = pkt->rx_ctrl.sig_len;

    if (len < 36) return;
    if (payload[0] != 0x80 || payload[1] != 0x00) return;

    uint8_t bssid[6];
    memcpy(bssid, payload + 16, 6);

    for (uint8_t i = 0; i < g_capturedBeaconCount; i++) {
        if (memcmp(g_capturedBeacons[i].bssid, bssid, 6) == 0) return;
    }

    uint64_t tsf;
    memcpy(&tsf, payload + 24, 8);

    uint8_t idx = g_capturedBeaconCount;
    memcpy(g_capturedBeacons[idx].bssid, bssid, 6);
    g_capturedBeacons[idx].tsf_us = tsf;
    g_capturedBeacons[idx].rssi = pkt->rx_ctrl.rssi;
    g_capturedBeaconCount++;
}

// Aktiviert den Promiscuous-Modus und startet die Beacon-Erfassung;
// speichert den aktuellen Unix-Zeitstempel als Referenz für spätere Berechnungen
static void startCapture() {
    g_capturedBeaconCount = 0;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    g_captureUnixUs = (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
    g_captureStartMs = millis();
    g_captureRunning = true;

    wifi_promiscuous_filter_t filter;
    filter.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(beaconRxCallback);
    esp_wifi_set_promiscuous(true);
}

// Beendet den Promiscuous-Modus und deregistriert den Beacon-Callback
static void stopCapture() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    g_captureRunning = false;
}

// Berechnet den Quality-Score eines Eintrags:
// score = (rssi + 100) + min(tsf_tage, 365)
// Lange Laufzeit (stabiler AP) wird stark gewichtet, RSSI ist sekundär.
static int calcScore(int8_t rssi, uint64_t tsf_us) {
    int rssi_score = (int)rssi + 100;
    if (rssi_score < 0) rssi_score = 0;
    uint64_t days = tsf_us / (86400ULL * 1000000ULL);
    if (days > 365) days = 365;
    return rssi_score + (int)days;
}

// Verarbeitet einen NTP-Snapshot: mergt neue APs per Quality-Score in die gespeicherte Liste.
// Bekannte APs werden aktualisiert (frischerer Messzeitpunkt = besserer Score).
// Unbekannte APs verdrängen nur den schwächsten Eintrag wenn ihr Score höher ist.
// Nicht gescannte APs bleiben unberührt in der Liste.
static void processSnapshot() {
    uint8_t count = g_capturedBeaconCount;

    if (count == 0) {
        serialPrint("TSF: Snapshot: keine APs");
        g_snapshotScheduled = false;
        return;
    }

    uint8_t newAdded = 0;
    uint8_t updated = 0;

    for (uint8_t n = 0; n < count; n++) {
        TsfEntry newEntry;
        memcpy(newEntry.bssid, g_capturedBeacons[n].bssid, 6);
        newEntry.tsf_us  = g_capturedBeacons[n].tsf_us;
        newEntry.unix_us = g_captureUnixUs;
        newEntry.rssi    = g_capturedBeacons[n].rssi;
        int newScore = calcScore(newEntry.rssi, newEntry.tsf_us);

        // Bereits bekannter AP: Eintrag mit frischen Messwerten überschreiben
        bool found = false;
        for (uint8_t s = 0; s < g_storedCount; s++) {
            if (memcmp(g_storedEntries[s].bssid, newEntry.bssid, 6) == 0) {
                g_storedEntries[s] = newEntry;
                updated++;
                found = true;
                break;
            }
        }
        if (found) continue;

        if (g_storedCount < MAX_TSF_ENTRIES) {
            g_storedEntries[g_storedCount++] = newEntry;
            newAdded++;
            continue;
        }

        // Liste voll: schwächsten Eintrag suchen und ggf. ersetzen
        uint8_t worstIdx = 0;
        int worstScore = calcScore(g_storedEntries[0].rssi, g_storedEntries[0].tsf_us);
        for (uint8_t s = 1; s < g_storedCount; s++) {
            int sc = calcScore(g_storedEntries[s].rssi, g_storedEntries[s].tsf_us);
            if (sc < worstScore) { worstScore = sc; worstIdx = s; }
        }
        if (newScore > worstScore) {
            g_storedEntries[worstIdx] = newEntry;
            newAdded++;
        }
    }

    saveTsfEntries(g_storedEntries, g_storedCount);
    g_lastSnapshotMs = millis();
    g_snapshotScheduled = false;

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: Snapshot %d neu, %d ersetzt, %d total",
             newAdded, updated, g_storedCount);
    logEvent(msg);
    serialPrint(msg);
}

// Berechnet die aktuelle Unix-Zeit aus TSF-Differenzen zwischen gespeichertem Snapshot
// und neuen Beacon-Messungen. Ausreißer werden per Sigma-Filterung entfernt, dann
// Median genommen. Einträge mit überlaufenem TSF-Zähler werden neu basiert.
static void processFallback() {
    uint8_t newCount = g_capturedBeaconCount;
    g_fallbackAttempted = true;

    if (newCount == 0 || g_storedCount == 0) {
        g_lastCalcResult = "insufficient";
        g_lastCalcAps = 0;
        logEvent("TSF: Fallback: keine Daten");
        g_fallbackScheduled = false;
        return;
    }

    uint64_t estimates[MAX_TSF_ENTRIES];
    uint8_t  estimateStoredIdx[MAX_TSF_ENTRIES];
    uint64_t rebaseNewTsf[MAX_TSF_ENTRIES];
    uint8_t  rebaseStoredIdx[MAX_TSF_ENTRIES];
    uint8_t  validCount = 0;
    uint8_t  rebaseCount = 0;

    for (uint8_t s = 0; s < g_storedCount; s++) {
        for (uint8_t n = 0; n < newCount; n++) {
            if (memcmp(g_storedEntries[s].bssid, g_capturedBeacons[n].bssid, 6) != 0) continue;

            uint64_t newTsf = g_capturedBeacons[n].tsf_us;
            uint64_t oldTsf = g_storedEntries[s].tsf_us;

            if (newTsf < oldTsf) {
                rebaseStoredIdx[rebaseCount] = s;
                rebaseNewTsf[rebaseCount]    = newTsf;
                rebaseCount++;
                break;
            }

            estimates[validCount]         = g_storedEntries[s].unix_us + (newTsf - oldTsf);
            estimateStoredIdx[validCount] = s;
            validCount++;
            break;
        }
    }

    if (validCount < 3) {
        g_lastCalcResult = "insufficient";
        g_lastCalcAps = validCount;
        char msg[52];
        snprintf(msg, sizeof(msg), "TSF: zu wenig APs (%d)", validCount);
        logEvent(msg);
        g_fallbackScheduled = false;
        return;
    }

    double sum = 0;
    for (uint8_t i = 0; i < validCount; i++) sum += (double)estimates[i];
    double mean = sum / validCount;

    double sumSq = 0;
    for (uint8_t i = 0; i < validCount; i++) {
        double d = (double)estimates[i] - mean;
        sumSq += d * d;
    }
    double sigma = sqrt(sumSq / validCount);
    const double maxDeviationUs = 30.0e6;

    uint64_t filtered[MAX_TSF_ENTRIES];
    uint8_t filteredCount = 0;
    for (uint8_t i = 0; i < validCount; i++) {
        double dev = fabs((double)estimates[i] - mean);
        if (dev > sigma && dev > maxDeviationUs) {
            uint8_t s = estimateStoredIdx[i];
            for (uint8_t n = 0; n < newCount; n++) {
                if (memcmp(g_storedEntries[s].bssid, g_capturedBeacons[n].bssid, 6) == 0) {
                    rebaseStoredIdx[rebaseCount] = s;
                    rebaseNewTsf[rebaseCount]    = g_capturedBeacons[n].tsf_us;
                    rebaseCount++;
                    break;
                }
            }
            continue;
        }
        filtered[filteredCount++] = estimates[i];
    }

    if (filteredCount < 3) {
        g_lastCalcResult = "insufficient";
        g_lastCalcAps = filteredCount;
        char msg[52];
        snprintf(msg, sizeof(msg), "TSF: zu wenig APs (%d)", filteredCount);
        logEvent(msg);
        g_fallbackScheduled = false;
        return;
    }

    std::sort(filtered, filtered + filteredCount);
    uint64_t medianUs = filtered[filteredCount / 2];

    double devSum = 0;
    for (uint8_t i = 0; i < filteredCount; i++) {
        double d = (double)filtered[i] - (double)medianUs;
        devSum += fabs(d);
    }
    uint32_t deviationMs = (uint32_t)(devSum / filteredCount / 1000.0);

    struct timeval tv;
    tv.tv_sec  = (time_t)(medianUs / 1000000ULL);
    tv.tv_usec = (suseconds_t)(medianUs % 1000000ULL);
    settimeofday(&tv, nullptr);

    if (rebaseCount > 0) {
        for (uint8_t i = 0; i < rebaseCount; i++) {
            uint8_t s = rebaseStoredIdx[i];
            g_storedEntries[s].tsf_us  = rebaseNewTsf[i];
            g_storedEntries[s].unix_us = medianUs;
            // RSSI des neu gescannten Beacons übernehmen (aktuellerer Messwert)
            for (uint8_t n = 0; n < newCount; n++) {
                if (memcmp(g_storedEntries[s].bssid, g_capturedBeacons[n].bssid, 6) == 0) {
                    g_storedEntries[s].rssi = g_capturedBeacons[n].rssi;
                    break;
                }
            }
        }
        saveTsfEntries(g_storedEntries, g_storedCount);
        char msg[52];
        snprintf(msg, sizeof(msg), "TSF: %d Eintraege neu basiert", rebaseCount);
        logEvent(msg);
    }

    g_lastCalcResult = "success";
    g_lastCalcAps = filteredCount;
    g_lastCalcDeviationMs = deviationMs;
    g_fallbackScheduled = false;

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: Zeit gesetzt %d APs +-%lums", filteredCount, (unsigned long)deviationMs);
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
    if (g_settings == nullptr || !g_settings->tsfFallbackEnabled) return;
    g_ntpSyncedTrigger = true;
}

// Manueller Snapshot-Auslöser (z.B. per Web-API); ignoriert laufende Scans
void triggerTsfSnapshot() {
    if (g_captureRunning) return;
    g_ntpSyncedTrigger = true;
}

// Hauptschleife: startet Snapshot-Scans nach NTP-Sync und Fallback-Scans wenn keine Uhrzeit verfügbar
void tsfClockLoop() {
    if (g_settings == nullptr || !g_settings->tsfFallbackEnabled) return;

    if (g_ntpSyncedTrigger && !g_snapshotScheduled && !g_captureRunning) {
        g_ntpSyncedTrigger = false;
        g_snapshotScheduled = true;
        g_fallbackAttempted = false;
        startCapture();
        serialPrint("TSF: Starte Snapshot-Scan");
        return;
    }

    bool staActive = WiFi.getMode() == WIFI_MODE_APSTA;
    if (!g_snapshotScheduled && !g_captureRunning && !g_fallbackAttempted
        && !g_fallbackScheduled && g_storedCount > 0 && staActive
        && g_tracker != nullptr && !g_tracker->clockSynced) {
        static unsigned long lastFallbackCheckMs = 0;
        if (isTimeForAction(&lastFallbackCheckMs, 60 * 1000)) {
            g_fallbackScheduled = true;
            startCapture();
            serialPrint("TSF: Starte Fallback-Scan");
            return;
        }
    }

    if (g_captureRunning && millis() - g_captureStartMs >= 2000) {
        stopCapture();
        if (g_snapshotScheduled) {
            processSnapshot();
        } else if (g_fallbackScheduled) {
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

    if (g_storedCount > 0) {
        int minSc = calcScore(g_storedEntries[0].rssi, g_storedEntries[0].tsf_us);
        int maxSc = minSc;
        for (uint8_t i = 1; i < g_storedCount; i++) {
            int sc = calcScore(g_storedEntries[i].rssi, g_storedEntries[i].tsf_us);
            if (sc < minSc) minSc = sc;
            if (sc > maxSc) maxSc = sc;
        }
        doc["minScore"] = minSc;
        doc["maxScore"] = maxSc;
    }

    String out;
    serializeJson(doc, out);
    return out;
}
