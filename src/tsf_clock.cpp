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
#include "clock_sync.h"
#include "wifi_controller.h" // msSinceNtpThisBoot
#include <climits>

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
static volatile bool g_fallbackTrigger = false; // Auftrag nach gescheitertem NTP-Versuch (requestTsfFallback)
static bool g_fallbackScheduled = false;

static unsigned long g_lastSnapshotMs = 0;
static const char* g_lastCalcResult = "none"; // nur String-Literale zuweisen (wird aus dem Web-Task gelesen)
static uint8_t g_lastCalcAps = 0;
static uint32_t g_lastCalcDeviationMs = 0; // mittlere Abweichung der AP-Schätzungen vom Median
static uint32_t g_lastCalcMaxDevMs = 0;    // größte Abweichung einer AP-Schätzung vom Median (schlechtester Fall)
static uint8_t g_lastCalcGen = 0;          // Generation der verwendeten Bezüge (0 = nur NTP-Anker)

// Snapshot von Hand nur so kurz nach einem NTP-Abgleich: dann ist die Systemzeit noch NTP-genau und
// darf als Generation-0-Anker gespeichert werden (sonst landete eine driftende Zeit als „NTP“ in der Liste)
#define MANUAL_SNAPSHOT_MAX_NTP_AGE_S 600

// Genauigkeitstest beim Snapshot: TSF-Zeit aus den bisherigen Bezugspunkten gegen die (per NTP gestellte)
// Systemzeit. Zeigt, wie weit TSF seit dem vorigen Snapshot höchstens danebengelegen hätte.
static uint8_t g_checkAps = 0;         // Anzahl verglichener APs (0 = noch kein Test)
static uint32_t g_checkMaxDevMs = 0;   // größte Abweichung eines APs
static uint32_t g_checkSpanS = 0;      // längster Abstand zum vorigen Bezugspunkt (s)

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

#define DRIFT_MIN_SPAN_US (2ULL * 3600ULL * 1000000ULL) // Gang erst ab 2 h Abstand messen (NTP-Schwankung ~ms)
#define DRIFT_MAX_PPB 200000                              // > 200 ppm ist kein plausibler Quarz → verwerfen

// Uhrzeit (µs) beim Empfang eines Beacons mit Zählerstand tsfNow, gerechnet aus dem Bezugspunkt e.
// Mit bekannter Gangabweichung wird die Zählerdifferenz auf echte Zeit umgerechnet (Gangkorrektur).
// Voraussetzung: tsfNow >= e.tsf_us (sonst wurde der AP neu gestartet).
static uint64_t estimateUnixUs(const TsfEntry& e, uint64_t tsfNow) {
    const int64_t d = (int64_t)(tsfNow - e.tsf_us);
    int64_t real = d;
    if(e.drift_ppb != TSF_DRIFT_UNKNOWN)
        real = d - d / 1000 * (int64_t)e.drift_ppb / 1000000; // d·(1 − drift); /1000 zuerst gegen Überlauf
    return e.unix_us + (uint64_t)real;
}

// Genauigkeitstest (vor dem Überschreiben der Bezugspunkte aufrufen): Für jeden gescannten, bereits
// gespeicherten AP die Uhrzeit so ausrechnen, wie der Fallback es täte, und mit der Systemzeit zum
// Empfangszeitpunkt vergleichen. APs mit zurückgesetztem Zähler oder > 30 s daneben (Neustart) zählen
// nicht. Ergebnis: größte Abweichung, Anzahl APs und längster Abstand zum vorigen Bezugspunkt.
static void checkAgainstSystemTime(uint8_t count) {
    uint8_t aps = 0;
    uint64_t maxDevUs = 0;
    uint64_t maxSpanUs = 0;
    for(uint8_t n = 0; n < count; n++) {
        const CapturedBeacon& b = g_capturedBeacons[n];
        for(uint8_t s = 0; s < g_storedCount; s++) {
            const TsfEntry& e = g_storedEntries[s];
            if(memcmp(e.bssid, b.bssid, 6) != 0)
                continue;
            // Nur NTP-Anker (Generation 0) zählen: abgeleitete Bezüge tragen schon einen Fehler in sich
            if(e.gen == 0 && b.tsf_us >= e.tsf_us) {
                const uint64_t actualUs = g_captureUnixUs + (uint64_t)(b.monoUs - g_captureMonoUs);
                const uint64_t estUs = estimateUnixUs(e, b.tsf_us); // wie der Fallback, mit Gangkorrektur
                const uint64_t devUs = estUs > actualUs ? estUs - actualUs : actualUs - estUs;
                if(devUs <= MAX_DEVIATION_US) {
                    aps++;
                    if(devUs > maxDevUs)
                        maxDevUs = devUs;
                    if(actualUs > e.unix_us && actualUs - e.unix_us > maxSpanUs)
                        maxSpanUs = actualUs - e.unix_us;
                }
            }
            break;
        }
    }
    if(aps == 0)
        return; // nichts vergleichbar (z. B. erster Snapshot) → vorigen Test stehen lassen
    g_checkAps = aps;
    g_checkMaxDevMs = (uint32_t)(maxDevUs / 1000ULL);
    g_checkSpanS = (uint32_t)(maxSpanUs / 1000000ULL);
    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: check %u APs max +-%lums", (unsigned)aps, (unsigned long)g_checkMaxDevMs);
    logEvent(msg);
    serialPrint(msg);
}

// Formatiert ppb als ppm mit Vorzeichen und 2 Nachkommastellen, z. B. 12400 → "+12.40"
static void formatPpm(char* buf, size_t len, int64_t ppb) {
    const char sign = ppb < 0 ? '-' : '+';
    const uint64_t a = (uint64_t)(ppb < 0 ? -ppb : ppb);
    snprintf(buf, len, "%c%lu.%02lu", sign, (unsigned long)(a / 1000), (unsigned long)((a % 1000) / 10));
}

// Protokolliert eine Gangmessung (zur Beobachtung, ob die Router-Zähler stabil laufen oder springen):
// letzte 3 Bytes der BSSID, gemessener Gang, Messdauer in Stunden, gemittelter (verwendeter) Gang.
// Beispiel: "TSF: drift 3a:4f:c1 +12.40ppm 5h avg +12.20"
static void logDrift(const uint8_t* bssid, int64_t measuredPpb, uint64_t spanUs, int32_t usedPpb) {
    char m[12], u[12], msg[52];
    formatPpm(m, sizeof(m), measuredPpb);
    formatPpm(u, sizeof(u), usedPpb);
    snprintf(msg, sizeof(msg), "TSF: drift %02x:%02x:%02x %sppm %luh avg %s", bssid[3], bssid[4], bssid[5], m,
             (unsigned long)((spanUs + 1800000000ULL) / 3600000000ULL), u);
    logEvent(msg);
    serialPrint(msg);
}

// Gangmessung beim Snapshot eines schon bekannten APs: neu = frischer NTP-Bezugspunkt (Messpunkt = er
// selbst, Gang unbekannt), alt = gespeicherter Eintrag. Ohne Neustart und mit Messpunkt ≥ 2 h zurück wird
// der Gang gemessen (Zählerlauf gegen NTP-Zeit) und mit dem bisherigen Wert gemittelt. Zu kurzer Abstand:
// alter Messpunkt und Gang bleiben, damit die nächste Messung über einen langen Abstand läuft.
// Nach einem Neustart (Zähler kleiner) bleibt der Gang (gleicher Quarz), der Messpunkt beginnt neu.
static void updateDrift(const TsfEntry& old, TsfEntry& neu) {
    neu.drift_ppb = old.drift_ppb;
    memcpy(neu.drift_hist, old.drift_hist, sizeof(neu.drift_hist)); // Verlauf bleibt erhalten
    if(old.rate_unix_us == 0 || neu.tsf_us < old.rate_tsf_us || neu.unix_us <= old.rate_unix_us)
        return; // kein Messpunkt oder AP neu gestartet → neuer Messpunkt (neu.rate_* = jetzt)
    const uint64_t spanUs = neu.unix_us - old.rate_unix_us;
    if(spanUs < DRIFT_MIN_SPAN_US) {
        neu.rate_tsf_us = old.rate_tsf_us; // Abstand noch zu kurz: alten Messpunkt behalten
        neu.rate_unix_us = old.rate_unix_us;
        return;
    }
    const int64_t tsfSpan = (int64_t)(neu.tsf_us - old.rate_tsf_us);
    const int64_t measured = (tsfSpan - (int64_t)spanUs) * 1000 / (int64_t)(spanUs / 1000000ULL) ; // ppb
    if(measured > DRIFT_MAX_PPB || measured < -DRIFT_MAX_PPB)
        return; // unplausibel → nicht übernehmen, neuer Messpunkt
    neu.drift_ppb = old.drift_ppb == TSF_DRIFT_UNKNOWN ? (int32_t)measured
                                                         : (int32_t)((old.drift_ppb + measured) / 2);
    // Messung vorne in den Verlauf schieben (wird mit der Liste im Flash gespeichert)
    memmove(&neu.drift_hist[1], &neu.drift_hist[0], (TSF_DRIFT_HIST - 1) * sizeof(int32_t));
    neu.drift_hist[0] = (int32_t)measured;
    logDrift(neu.bssid, measured, spanUs, neu.drift_ppb);
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

    checkAgainstSystemTime(count);

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
        newEntry.gen = 0; // Snapshot nur mit NTP-genauer Systemzeit (automatisch nach NTP oder frisch von Hand)
        newEntry.drift_ppb = TSF_DRIFT_UNKNOWN;
        newEntry.rate_tsf_us = newEntry.tsf_us; // dieser NTP-Punkt ist Messpunkt für die nächste Gangmessung
        newEntry.rate_unix_us = newEntry.unix_us;
        for(uint8_t h = 0; h < TSF_DRIFT_HIST; h++)
            newEntry.drift_hist[h] = TSF_DRIFT_UNKNOWN;
        int newScore = calcScore(newEntry.rssi, newEntry.tsf_us);

        // Bereits bekannter AP: Eintrag mit frischen Messwerten überschreiben
        bool found = false;
        for(uint8_t s = 0; s < g_storedCount; s++) {
            if(memcmp(g_storedEntries[s].bssid, newEntry.bssid, 6) == 0) {
                updateDrift(g_storedEntries[s], newEntry);
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
    uint8_t gen;      // Generation des verwendeten Bezugs
};

// Berechnet die aktuelle Unix-Zeit aus TSF-Differenzen zwischen gespeicherten Bezugspunkten und neuen
// Beacon-Messungen — immer absolut aus den Bezügen, nie aufbauend auf einer früheren TSF-Uhrzeit.
// Alle gehörten Bezüge zählen (NTP-Anker und abgeleitete): Der Median verhindert, dass ein abgeleiteter
// Bezug Fehler weiterträgt, und mehr APs machen das Ergebnis robuster. Eine Simulation (7 Tage ohne NTP,
// Berechnung alle 5 h) zeigte: Fehler addieren sich so nicht; der Langzeitfehler ist die Quarz-Drift der
// APs seit dem letzten NTP. Eine Beschränkung auf NTP-Anker brachte keine Genauigkeit, aber 20× öfter
// „zu wenige APs“.
//  1. Ausreißer (> 30 s vom Median) werden verworfen, der Median der übrigen wird Systemzeit.
//  2. Neu gestartete APs (Zähler zurückgesetzt) und Ausreißer werden neu basiert, mit Generation
//     = höchste Generation der verwendeten Bezüge + 1 (nur zur Information: wie weit vom NTP-Anker).
static void processFallback() {
    g_fallbackScheduled = false;
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
    bool rebaseOutlier[MAX_TSF_ENTRIES]; // true = Ausreißer (evtl. anderes Gerät), false = Neustart (gleicher Quarz)
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
                rebaseOutlier[rebaseCount] = false;
                rebaseCount++;
            } else {
                // Zeit beim Empfang = Snapshot-Zeit + TSF-Differenz; plus die Zeit seit dem Empfang
                est[estCount++] = {estimateUnixUs(e, b.tsf_us) + (uint64_t)(nowMonoUs - b.monoUs), s, n, e.gen};
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
            rebaseOutlier[rebaseCount] = true;
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
    // Höchste Generation der verwendeten Bezüge (0 = nur NTP-Anker); neu basierte liegen eine Stufe darüber
    uint8_t gStar = 0;
    for(uint8_t i = 0; i < keptCount; i++)
        if(kept[i].gen > gStar)
            gStar = kept[i].gen;
    const uint8_t rebaseGen = gStar < 254 ? gStar + 1 : 255;
    if(medianNowUs < MIN_VALID_EPOCH_US) {
        g_lastCalcResult = "invalid";
        g_lastCalcAps = keptCount;
        logEvent("TSF: invalid reference time");
        return;
    }

    // Abweichung jeder verwendeten AP-Schätzung vom Median: Mittelwert (Log) und Maximum (Anzeige)
    uint64_t devSum = 0, devMax = 0;
    for(uint8_t i = 0; i < keptCount; i++) {
        uint64_t dev = kept[i].unixUs > medianNowUs ? kept[i].unixUs - medianNowUs : medianNowUs - kept[i].unixUs;
        devSum += dev;
        if(dev > devMax)
            devMax = dev;
    }
    uint32_t deviationMs = (uint32_t)(devSum / keptCount / 1000ULL);

    // Die Rechenzeit seit nowMonoUs (wenige µs bis ms) noch dazurechnen
    const uint64_t setUs = medianNowUs + (uint64_t)(esp_timer_get_time() - nowMonoUs);
    struct timeval tv;
    tv.tv_sec = (time_t)(setUs / 1000000ULL);
    tv.tv_usec = (suseconds_t)(setUs % 1000000ULL);
    settimeofday(&tv, nullptr);
    noteClockSync(CLOCK_SYNC_TSF); // für den Systemmonitor
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
            e.gen = rebaseGen; // abgeleitet: eine Stufe schlechter als die verwendeten Bezüge
            e.rate_unix_us = 0; // kein NTP-Messpunkt mehr (Zeit nur aus TSF)
            // Neustart: gleicher Quarz → Gang bleibt. Ausreißer: womöglich anderes Gerät → Gang verwerfen
            if(rebaseOutlier[i]) {
                e.drift_ppb = TSF_DRIFT_UNKNOWN;
                for(uint8_t h = 0; h < TSF_DRIFT_HIST; h++)
                    e.drift_hist[h] = TSF_DRIFT_UNKNOWN; // Verlauf gehörte womöglich zu einem anderen Gerät
            }
        }
        saveTsfEntries(g_storedEntries, g_storedCount);
        char msg[52];
        snprintf(msg, sizeof(msg), "TSF: %d entries rebased", rebaseCount);
        logEvent(msg);
    }

    g_lastCalcResult = "success";
    g_lastCalcAps = keptCount;
    g_lastCalcDeviationMs = deviationMs;
    g_lastCalcMaxDevMs = (uint32_t)(devMax / 1000ULL);
    g_lastCalcGen = gStar;

    char msg[52];
    snprintf(msg, sizeof(msg), "TSF: time set, %d APs gen %u +-%lums", keptCount, (unsigned)gStar,
             (unsigned long)g_lastCalcMaxDevMs);
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

// Meldet, ob ein Snapshot aussteht oder die Erfassung läuft; bei abgeschaltetem Modul nie beschäftigt
bool isTsfCaptureBusy() {
    if(g_settings == nullptr || !g_settings->tsfFallbackEnabled)
        return false;
    return g_ntpSyncedTrigger || g_fallbackTrigger || g_captureRunning;
}

// Nach einem gescheiterten NTP-Versuch: Beacons erfassen, Uhrzeit aus den Bezugspunkten berechnen und
// neu gestartete/abweichende APs neu basieren. Ohne gespeicherte Bezugspunkte gibt es nichts zu rechnen.
// Das WLAN muss dafür noch an sein (der Aufrufer wartet über isTsfCaptureBusy()).
void requestTsfFallback() {
    if(g_settings == nullptr || !g_settings->tsfFallbackEnabled || g_storedCount == 0)
        return;
    g_fallbackTrigger = true;
}

// true, wenn ein Snapshot von Hand erlaubt ist: NTP-Abgleich in dieser Wachphase höchstens 10 min her.
// Wach läuft die Systemzeit über den Hauptquarz (≤ ~20 ppm → ≤ ~12 ms in 10 min); nach einem Deep Sleep
// ist sie über den RC-Takt gelaufen (≈ 1,5 %) und darf nicht als NTP-Anker gespeichert werden.
bool isTsfSnapshotAllowed() {
    return msSinceNtpThisBoot() <= MANUAL_SNAPSHOT_MAX_NTP_AGE_S * 1000UL;
}

// Manueller Snapshot-Auslöser (z.B. per Web-API); ignoriert laufende Scans und zu alte NTP-Zeit
void triggerTsfSnapshot() {
    if(!isTsfSnapshotAllowed())
        return;
    if(g_captureRunning)
        return;
    g_ntpSyncedTrigger = true;
}

// Hauptschleife: startet nach jedem Sync-Versuch eine Beacon-Erfassung — nach NTP-Erfolg als Snapshot
// (neue Bezugspunkte), nach Fehlschlag als Fallback (Uhrzeit berechnen) — und wertet sie nach 2 s aus
void tsfClockLoop() {
    if(g_settings == nullptr || !g_settings->tsfFallbackEnabled)
        return;

    if(g_ntpSyncedTrigger && !g_snapshotScheduled && !g_captureRunning) {
        g_ntpSyncedTrigger = false;
        if(startCapture()) {
            g_snapshotScheduled = true;
            serialPrint("TSF: Starte Snapshot-Scan");
        } else {
            logEvent("TSF: scan start failed");
        }
        return;
    }

    if(g_fallbackTrigger && !g_snapshotScheduled && !g_captureRunning) {
        g_fallbackTrigger = false;
        if(startCapture()) {
            g_fallbackScheduled = true;
            serialPrint("TSF: Starte Fallback-Scan");
        } else {
            logEvent("TSF: scan start failed");
        }
        return;
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

// Gibt TSF-Modul-Status als JSON zurück: aktiviert, gespeicherte APs, letzter Snapshot (ms her),
// Ergebnis der letzten Berechnung mit Anzahl APs, mittlerer und größter Abweichung (ms),
// Genauigkeitstest beim letzten Snapshot (APs, größte Abweichung in ms, Abstand zum vorigen Bezugspunkt in s)
String getTsfStatusJson() {
    JsonDocument doc;
    doc["enabled"] = g_settings != nullptr && g_settings->tsfFallbackEnabled;
    doc["storedAps"] = g_storedCount;
    doc["lastSnapshotMs"] = g_lastSnapshotMs > 0 ? (unsigned long)(millis() - g_lastSnapshotMs) : 0;
    doc["lastCalcResult"] = g_lastCalcResult;
    doc["lastCalcAps"] = g_lastCalcAps;
    doc["lastCalcDeviationMs"] = g_lastCalcDeviationMs;
    doc["lastCalcMaxDevMs"] = g_lastCalcMaxDevMs;
    doc["lastCalcGen"] = g_lastCalcGen;
    uint8_t gen0 = 0;
    for(uint8_t i = 0; i < g_storedCount; i++)
        if(g_storedEntries[i].gen == 0)
            gen0++;
    doc["storedNtpAps"] = gen0; // davon mit NTP-Anker (Generation 0)
    uint8_t withDrift = 0;
    for(uint8_t i = 0; i < g_storedCount; i++)
        if(g_storedEntries[i].drift_ppb != TSF_DRIFT_UNKNOWN)
            withDrift++;
    doc["storedDriftAps"] = withDrift; // davon mit gemessener Gangabweichung
    // Je AP: Kennung (letzte 3 BSSID-Bytes), Generation, verwendeter Gang und Verlauf der Messungen (ppb)
    JsonArray aps = doc["aps"].to<JsonArray>();
    for(uint8_t i = 0; i < g_storedCount; i++) {
        const TsfEntry& e = g_storedEntries[i];
        JsonObject o = aps.add<JsonObject>();
        char id[9];
        snprintf(id, sizeof(id), "%02x:%02x:%02x", e.bssid[3], e.bssid[4], e.bssid[5]);
        o["id"] = id;
        o["gen"] = e.gen;
        if(e.drift_ppb != TSF_DRIFT_UNKNOWN)
            o["drift"] = e.drift_ppb;
        JsonArray h = o["hist"].to<JsonArray>();
        for(uint8_t k = 0; k < TSF_DRIFT_HIST; k++)
            if(e.drift_hist[k] != TSF_DRIFT_UNKNOWN)
                h.add(e.drift_hist[k]);
    }
    doc["snapshotAllowed"] = isTsfSnapshotAllowed();
    // Genauigkeitstest beim letzten Snapshot (checkAps = 0: noch keiner)
    doc["checkAps"] = g_checkAps;
    doc["checkMaxDevMs"] = g_checkMaxDevMs;
    doc["checkSpanS"] = g_checkSpanS;

    String out;
    serializeJson(doc, out);
    return out;
}