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

static void stopCapture() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    g_captureRunning = false;
}

static void processSnapshot() {
    uint8_t count = g_capturedBeaconCount;

    if (count == 0) {
        serialPrint("TSF: Snapshot: keine APs");
        g_snapshotScheduled = false;
        return;
    }

    for (uint8_t i = 0; i < count - 1; i++) {
        uint8_t best = i;
        for (uint8_t j = i + 1; j < count; j++) {
            if (g_capturedBeacons[j].rssi > g_capturedBeacons[best].rssi) best = j;
        }
        if (best != i) {
            CapturedBeacon tmp = g_capturedBeacons[i];
            g_capturedBeacons[i] = g_capturedBeacons[best];
            g_capturedBeacons[best] = tmp;
        }
    }

    uint8_t saveCount = count < MAX_TSF_ENTRIES ? count : MAX_TSF_ENTRIES;
    g_storedCount = saveCount;
    for (uint8_t i = 0; i < saveCount; i++) {
        memcpy(g_storedEntries[i].bssid, g_capturedBeacons[i].bssid, 6);
        g_storedEntries[i].tsf_us = g_capturedBeacons[i].tsf_us;
        g_storedEntries[i].unix_us = g_captureUnixUs;
    }

    saveTsfEntries(g_storedEntries, g_storedCount);
    g_lastSnapshotMs = millis();
    g_snapshotScheduled = false;

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: Snapshot %d APs", saveCount);
    logEvent(msg);
    serialPrint(msg);
}

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

void initTsfClock(const SystemSettings* settings, WiFiTestTracker* tracker) {
    g_settings = settings;
    g_tracker = tracker;
    g_storedCount = loadTsfEntries(g_storedEntries, MAX_TSF_ENTRIES);

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: Init, %d APs geladen", g_storedCount);
    serialPrint(msg);
}

void onNtpSynced() {
    if (g_settings == nullptr || !g_settings->tsfFallbackEnabled) return;
    g_ntpSyncedTrigger = true;
}

void triggerTsfSnapshot() {
    if (g_captureRunning) return;
    g_ntpSyncedTrigger = true;
}

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

String getTsfStatusJson() {
    JsonDocument doc;
    doc["enabled"] = g_settings != nullptr && g_settings->tsfFallbackEnabled;
    doc["storedAps"] = g_storedCount;
    doc["lastSnapshotMs"] = g_lastSnapshotMs > 0 ? (unsigned long)(millis() - g_lastSnapshotMs) : 0;
    doc["lastCalcResult"] = g_lastCalcResult;
    doc["lastCalcAps"] = g_lastCalcAps;
    doc["lastCalcDeviationMs"] = g_lastCalcDeviationMs;

    String out;
    serializeJson(doc, out);
    return out;
}
