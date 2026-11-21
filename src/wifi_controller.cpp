// WiFi-Controller: verwaltet AP-, STA- und APSTA-Modus, NTP-Synchronisation,
// automatischen Start/Stop und die Reconnect-Logik mit Fallback-WLAN.
// WiFi-Events und der NTP-Callback laufen in fremden Tasks und legen nur Nachrichten
// in eine Queue; die eigentliche Arbeit passiert in wifiLoop() im Hauptthread.
#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <limits.h>
#include <sys/time.h>
#include <time.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>
#include "json_utils.h" // PASSWORD_MASK
#include "wifi_controller.h"
#include "captive_portal.h"
#include "debug_utils.h"
#include "route_handlers.h"
#include "types.h"
#include "system_utils.h"
#include "store.h"
#include "event_log.h"
#include "esp_sntp.h"
#include "tsf_clock.h"
#include "clock_sync.h"

#define MAX_CLIENTS 4
// Sendeleistung für Hotspot UND Heimnetz: 17 dBm (aus Messungen). Mit der Standardleistung des
// C3 Super Mini scheitert die Anmeldung am Router oft mit Grund 2 (Anmeldung abgelaufen).
#define WIFI_TX_POWER WIFI_POWER_17dBm
#define DNS_INTERVAL 30             // DNS-Anfragen alle 30ms verarbeiten
#define TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3" // Mitteleuropäische Zeit mit Sommerzeit
#define MIN_VALID_EPOCH 1577836800L // 2020-01-01: alles davor gilt als "Uhrzeit unbekannt"

static int s_wifiChannel = 1; // wird in initWiFiController() per Scan gesetzt
// Zuletzt gewählter Kanal im RTC-Speicher: übersteht Deep Sleep (nicht Reset/Stromausfall).
// Nach dem Aufwachen wird er wiederverwendet → kein 1–3 s blockierender Kanal-Scan beim Start.
RTC_DATA_ATTR static int s_cachedChannel = 0;

static void processWifiEvents();

// Scannt Kanal 1/6/11 und wählt den am wenigsten belasteten.
// Last = Summe der Signalstärken aller Netze auf dem Kanal und seinen Nachbarkanälen
static int bestChannel() {
    serialPrint("WiFi: scanne besten Kanal...");
    WiFi.mode(WIFI_MODE_STA);
    int found = WiFi.scanNetworks(false, true);
    int candidates[] = {1, 6, 11};
    int best = 1, bestLoad = INT_MAX;
    for(int cand : candidates) {
        int load = 0;
        for(int i = 0; i < found; i++) {
            // Kanäle bis 3 Nummern entfernt überlappen noch spürbar
            if(abs((int)WiFi.channel(i) - cand) <= 3) {
                int weight = 100 + (int)WiFi.RSSI(i); // starkes Signal = mehr Last
                if(weight > 0)
                    load += weight;
            }
        }
        serialPrint("WiFi: Kanal " + String(cand) + " Last=" + String(load));
        if(load < bestLoad) {
            bestLoad = load;
            best = cand;
        }
    }
    WiFi.scanDelete();
    serialPrint("WiFi: bester Kanal=" + String(best));
    return best;
}

// Feste IP für den eigenen Hotspot (AP-Modus) — Samsung-Geräte brauchen eine öffentliche IP
static const IPAddress localIP(4, 3, 2, 1);
static const IPAddress subnetMask(255, 255, 255, 0);
static const SystemSettings* g_systemSettings = nullptr;
static std::vector<Route> g_routes;
static WiFiTestTracker* g_wifiTracker = nullptr;
static uint8_t s_staIdx = 0; // 0 = primäres WLAN, 1 = Fallback-WLAN
static int s_retryCount = 0; // direkte Wiederholungsversuche nach Verbindungsabbruch

// WLAN-Suche / Verbindungstest (Einstellungen → WLAN): solange aktiv, ruht die normale STA-Logik
static volatile bool s_toolActive = false;
static unsigned long s_toolQuietUntilMs = 0;          // kurz nach Ende noch Trennungs-Events ignorieren
static volatile uint8_t s_lastDisconnectReason = 0;   // aus dem Event-Task (Grund der letzten Trennung)
static void wifiToolsLoop();

// Nach erfolgreichem NTP-Abgleich: Heimnetz-Verbindung trennen, sobald der TSF-Snapshot fertig ist
static bool s_dropStaPending = false;

// Ergebnis des letzten Sync-Versuchs: beim Verbindungsaufbau false, bei NTP-Erfolg true.
// Bestimmt, ob der nächste automatische Abgleich nach syncIntervalMin oder syncRetryMin startet.
static bool s_lastSyncOk = false;
static unsigned long s_staStartMs = 0; // Beginn des aktuellen Verbindungsversuchs (für STA_SYNC_TIMEOUT_MS)
static unsigned long s_syncTimerMs = 0; // Ende der letzten Heimnetz-Phase (Startpunkt des Sync-Intervalls)
static void dropStaAfterSync();

// Nach gescheitertem NTP-Versuch: Heimnetz erst abschalten, wenn der TSF-Fallback gerechnet hat
static bool s_syncFailPending = false;
static void dropStaAfterFail();
static void syncFailed(const char* why);

// Verbindet mit dem externen WLAN an Index idx; fällt auf primäres zurück wenn kein Fallback konfiguriert
static void beginSta(uint8_t idx) {
    bool useFallback = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0);
    const char* ssid = useFallback ? g_systemSettings->externalSSID2 : g_systemSettings->externalSSID;
    const char* pw = useFallback ? g_systemSettings->externalPW2 : g_systemSettings->externalPW;
    s_staIdx = useFallback ? 1 : 0;
    s_lastSyncOk = false; // neuer Versuch: gilt als fehlgeschlagen, bis NTP antwortet
    s_staStartMs = millis();
    char buf[52]; // passt zu EVENT_MSG_LEN: 24 Zeichen Präfix + 27 SSID-Zeichen
    snprintf(buf, sizeof(buf), "STA connect%s: %.27s", useFallback ? " (fallback)" : "", ssid);
    logEvent(buf);
    WiFi.setTxPower(WIFI_TX_POWER); // nach WiFi.mode(): sonst Standardleistung → Anmeldung scheitert
    WiFi.begin(ssid, pw);
}

// Startet den eigenen Hotspot (WiFi-Modus AP oder APSTA muss schon gesetzt sein)
static bool startAccessPoint(const char* tag) {
    const SystemSettings* s = g_systemSettings;
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT40);
    WiFi.setTxPower(WIFI_TX_POWER);
    delay(100);
    serialPrint("setTxPower=" + String(WiFi.getTxPower()) + " target=" + String(WIFI_TX_POWER));
    WiFi.softAPConfig(localIP, localIP, subnetMask);
    const char* pw = strlen(s->internalPW) > 0 ? s->internalPW : nullptr;
    bool ok = WiFi.softAP(s->internalSSID, pw, s_wifiChannel, 0, MAX_CLIENTS);
    serialPrint(String("softAP(") + tag + ")=" + (ok ? "OK" : "FAIL") + " ch=" + String(s_wifiChannel)
                + " IP=" + WiFi.softAPIP().toString());
    return ok;
}

DNSServer dnsServer;
AsyncWebServer server(80);
static bool serverStarted = false; // server.begin() darf nur einmal aufgerufen werden

// --- Nachrichten aus fremden Tasks (WiFi-Event-Task, lwIP-Task) an den Hauptthread -------------

enum WifiMsgType : uint8_t { MSG_STA_START, MSG_GOT_IP, MSG_DISCONNECTED, MSG_NTP_SYNCED };

struct WifiMsg {
    WifiMsgType type;
    bool hasDrift;
    int32_t driftMs; // MSG_NTP_SYNCED: Abweichung in ms; MSG_DISCONNECTED: Trennungsgrund (wifi_err_reason_t)
};

static QueueHandle_t s_msgQueue = nullptr;

// Darf aus jedem Task aufgerufen werden, blockiert nie (bei voller Queue geht die Nachricht verloren)
static void postMsg(WifiMsgType type, bool hasDrift = false, int32_t driftMs = 0) {
    if(s_msgQueue == nullptr)
        return;
    WifiMsg m = {type, hasDrift, driftMs};
    xQueueSend(s_msgQueue, &m, 0);
}

// Uhrzeit kurz vor sntp_restart() — zum Berechnen des Zeitdrifts nach der Synchronisation
static int64_t s_preSyncMonoUs = 0;  // esp_timer-Zeit (monoton) zum Zeitpunkt der Messung
static int64_t s_preSyncEpochUs = 0; // RTC-Zeit in µs; 0 = keine gültige Referenz

// Callback von der SNTP-Bibliothek (läuft im lwIP-Task): nur Drift berechnen und weiterreichen
static void ntpSyncCallback(struct timeval* tv) {
    if(tv == nullptr)
        return;
    bool hasDrift = false;
    int32_t driftMs = 0;
    if(s_preSyncEpochUs > 0) {
        int64_t newUs = (int64_t)tv->tv_sec * 1000000LL + tv->tv_usec;
        // Soviel Zeit war seit der Messung vergangen: so spät hätte die alte Uhr jetzt gezeigt
        int64_t expectedUs = s_preSyncEpochUs + (esp_timer_get_time() - s_preSyncMonoUs);
        driftMs = (int32_t)((newUs - expectedUs) / 1000);
        hasDrift = true;
    }
    s_preSyncEpochUs = 0;
    postMsg(MSG_NTP_SYNCED, hasDrift, driftMs);
}

// Initialisiert den WiFi-Controller einmalig: Zeitzone, DNS, Webserver, Event-Handler
void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker) {
    serialPrint("Initializing WiFi controller");
    serialPrint(String("Internal SSID: ") + settings.internalSSID);
    serialPrint(String("External SSID: ") + settings.externalSSID);

    g_systemSettings = &settings;
    g_routes = routes;
    g_wifiTracker = &tracker;

    s_msgQueue = xQueueCreate(8, sizeof(WifiMsg));

    // Zeitzone: Mitteleuropäische Zeit mit Sommerzeit
    configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");
    sntp_set_time_sync_notification_cb(ntpSyncCallback);
    WiFi.onEvent(WiFiEvent);
    WiFi.persistent(false); // keine Credentials in NVS speichern
    WiFi.setSleep(false);   // sleep=aus: bessere Empfangsqualität (Testergebnis)
    // Nach Deep Sleep den gemerkten Kanal nehmen, sonst (Einschalten/Reset) neu scannen
    bool fromDeepSleep = esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED;
    if(fromDeepSleep && (s_cachedChannel == 1 || s_cachedChannel == 6 || s_cachedChannel == 11)) {
        s_wifiChannel = s_cachedChannel;
        serialPrint("WiFi: Kanal " + String(s_wifiChannel) + " aus RTC-Speicher (kein Scan)");
    } else {
        s_wifiChannel = bestChannel();
        s_cachedChannel = s_wifiChannel;
    }
    WiFi.mode(WIFI_MODE_NULL);

    setUpDNSServer(dnsServer, localIP);
    setUpWebserver(server, localIP, g_routes);

    serialPrint("WiFi controller initialized");
}

// Läuft in der Hauptschleife: Events, DNS-Anfragen, Auto-Start/Stop, Telemetrie, Reconnect
void wifiLoop() {
    processWifiEvents();
    dropStaAfterSync();
    dropStaAfterFail();
    wifiToolsLoop();
    handleClientRequests();
    checkWifiStart();
    checkWifiStop();
    updateTelemetryWiFiStatus();

    // WLAN-Status jede Minute auf Serial (Debug)
    static unsigned long lastStatusMs = 0;
    if(isTimeForAction(&lastStatusMs, 60000)) {
        if(!isWiFiActive()) {
            serialPrint("WiFi: aus");
        } else {
            uint8_t primaryCh = 0;
            wifi_second_chan_t secondCh;
            esp_wifi_get_channel(&primaryCh, &secondCh);
            wifi_country_t country = {};
            esp_wifi_get_country(&country);
            char cc[3] = {country.cc[0], country.cc[1], '\0'}; // cc ist nicht null-terminiert
            serialPrint("WiFi: mode=" + String((int)WiFi.getMode()) + " ch=" + String(primaryCh)
                        + " IP=" + WiFi.softAPIP().toString() + " clients=" + String(WiFi.softAPgetStationNum())
                        + " txpwr=" + String(WiFi.getTxPower()) + " country=" + String(cc));
        }
    }
}

// Verarbeitet DNS-Anfragen für das Captive-Portal (nur im AP- oder APSTA-Modus)
void handleClientRequests() {
    static unsigned long lastDnsProcessMs = 0;
    if(!isTimeForAction(&lastDnsProcessMs, DNS_INTERVAL))
        return;

    wifi_mode_t currentMode = WiFi.getMode();
    if(currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA) {
        dnsServer.processNextRequest();
    }
}

static unsigned long s_wifiStopTimerMs = 0;

// Hotspot-Bedienzeit: letzte echte Web-Anfrage (Seite laden, Einstellungen lesen/speichern).
// Wird aus dem async_tcp-Task geschrieben; 32-Bit-Zugriffe sind auf dem ESP32 atomar.
// Die Laufzeit ohne Bedienung steht in SystemSettings::apTimeoutMin (0 = unbegrenzt).
static volatile unsigned long s_lastWebActivityMs = 0;

// Merkt sich eine echte Bedienung der Weboberfläche (verlängert die Hotspot-Laufzeit)
void noteWebActivity() {
    s_lastWebActivityMs = millis();
}

// Schaltet nur den Hotspot ab: APSTA → STA (Heimnetz bleibt), AP → WLAN aus. Ohne Hotspot nichts.
static void apOff(const char* logMsg) {
    wifi_mode_t mode = WiFi.getMode();
    if(mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)
        return;
    logEvent(logMsg);
    dnsServer.stop();
    WiFi.mode(mode == WIFI_MODE_APSTA ? WIFI_MODE_STA : WIFI_MODE_NULL);
}

// Schaltet nur das Heimnetz ab: APSTA → AP (Hotspot bleibt), STA → WLAN aus. Ohne STA nichts.
static void staOff(const char* logMsg) {
    wifi_mode_t mode = WiFi.getMode();
    if(mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)
        return;
    logEvent(logMsg);
    g_wifiTracker->staConfigValid = false;
    WiFi.mode(mode == WIFI_MODE_APSTA ? WIFI_MODE_AP : WIFI_MODE_NULL);
}

// Automatisches Abschalten — Hotspot und Heimnetz getrennt, jeder Modus nach eigenen Regeln:
// Hotspot: nach apTimeoutMin ohne Bedienung oder STOP_WIFI_AFTER_MS (1 min) ohne verbundenes Gerät;
//          apTimeoutMin = 0 → bleibt an, bis er von Hand ausgeschaltet wird.
// Heimnetz: nach STA_SYNC_TIMEOUT_MS ohne abgeschlossenen Abgleich (Erfolg trennt dropStaAfterSync()).
void checkWifiStop() {
    wifi_mode_t mode = WiFi.getMode();
    bool apOn = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    bool staOn = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);

    // --- Hotspot ---
    unsigned long apTimeoutMs = (unsigned long)g_systemSettings->apTimeoutMin * 60UL * 1000UL;
    if(!apOn || apTimeoutMs == 0 || hasWifiClients()) {
        s_wifiStopTimerMs = millis();
    } else if(isTimeForAction(&s_wifiStopTimerMs, STOP_WIFI_AFTER_MS)) {
        apOff("Hotspot off: no client");
    }
    // Ohne Bedienung → aus, auch wenn noch ein Gerät verbunden ist (Handys bleiben oft im Hintergrund
    // verbunden; sonst liefe der Hotspot ewig und verhinderte den Deep Sleep)
    if(apOn && apTimeoutMs > 0 && millis() - s_lastWebActivityMs >= apTimeoutMs) {
        apOff("Hotspot off: idle timeout");
    }

    // --- Heimnetz (nur für den Zeitabgleich) ---
    // Nicht während WLAN-Suche/-Test und nicht, solange nach Erfolg noch der TSF-Snapshot läuft
    if(staOn && !s_toolActive && !s_dropStaPending && !s_syncFailPending && !isTsfCaptureBusy()
       && millis() - s_staStartMs >= STA_SYNC_TIMEOUT_MS) {
        syncFailed("STA failed: sync timeout");
    }
}

// Heimnetz-Verbindung gerade aktiv (STA oder APSTA) — nur während eines Zeitabgleichs bzw. Tests
bool isStaActive() {
    wifi_mode_t mode = WiFi.getMode();
    return mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA;
}

// Ergebnis des letzten Abgleich-Versuchs (true = NTP hat geantwortet)
bool lastSyncOk() {
    return s_lastSyncOk;
}

// Restzeit bis zum nächsten automatischen Abgleich in ms (für die Statusanzeige):
// 0 = läuft gerade, -1 = kein Heimnetz eingetragen
long msUntilNextSync() {
    if(strlen(g_systemSettings->externalSSID) == 0)
        return -1;
    if(isStaActive())
        return 0;
    unsigned long elapsed = millis() - s_syncTimerMs;
    unsigned long interval = nextSyncIntervalMs();
    return elapsed >= interval ? 0 : (long)(interval - elapsed);
}

// Wartezeit bis zum nächsten automatischen Zeitabgleich: nach Erfolg syncIntervalMin, nach einem
// Fehlschlag syncRetryMin. Ohne eingetragenes Heimnetz gibt es nichts zu wiederholen → syncIntervalMin.
// Auch der Deep Sleep schläft höchstens so lange (sleep_manager.cpp).
unsigned long nextSyncIntervalMs() {
    bool retry = !s_lastSyncOk && strlen(g_systemSettings->externalSSID) > 0;
    uint16_t minutes = retry ? g_systemSettings->syncRetryMin : g_systemSettings->syncIntervalMin;
    return (unsigned long)minutes * 60UL * 1000UL;
}

// Einziger Zeitgeber für den Zeitabgleich übers Heimnetz — unabhängig vom Hotspot. Gezählt wird ab
// dem Ende der letzten Heimnetz-Phase (STA aus); nach nextSyncIntervalMs() folgt ein neuer Versuch:
// WLAN aus → nur STA starten, Hotspot läuft → STA dazuschalten (APSTA). Während WLAN-Suche/-Test ruht er.
void checkWifiStart() {
    if(isStaActive() || s_toolActive || strlen(g_systemSettings->externalSSID) == 0) {
        s_syncTimerMs = millis();
        return;
    }
    if(!isTimeForAction(&s_syncTimerMs, nextSyncIntervalMs()))
        return;

    startWifi(false); // nur Heimnetz; ein laufender Hotspot bleibt unberührt
}

// Zeitpunkt (millis) des letzten erfolgreichen NTP-Syncs; nur im Hauptthread (processWifiEvents) gesetzt.
// Eigenes Flag, weil sntp_get_sync_status() COMPLETED nur einmal meldet und danach auf RESET fällt.
static unsigned long s_lastNtpSyncMs = 0;

// Millisekunden seit dem letzten NTP-Erfolg in dieser Wachphase; ULONG_MAX = keiner seit Start/Aufwachen.
// Bewusst über millis() (Hauptquarz, beginnt nach jedem Deep Sleep neu): Nach einem Deep Sleep ist die
// Systemzeit über den ungenauen RC-Takt gelaufen und taugt nicht mehr als NTP-genauer TSF-Anker.
unsigned long msSinceNtpThisBoot() {
    if(!g_wifiTracker || !g_wifiTracker->clockSynced || s_lastNtpSyncMs == 0)
        return ULONG_MAX;
    return millis() - s_lastNtpSyncMs;
}

#define NTP_STALE_AFTER_MS (24UL * 60UL * 60UL * 1000UL)   // älter als 24 h → NTP-Zeit gilt als veraltet
#define NTP_SAVE_INTERVAL_MS (30UL * 60UL * 1000UL)        // NVS-Zeitstempel nur alle 30 min (schont den Flash)

// Prüft einmal pro Minute, ob der letzte NTP-Sync noch frisch ist (< 24 h), und sichert
// alle 30 Minuten die aktuelle Zeit in NVS. clockSynced wird sonst nur im NTP-Callback-Zweig gesetzt.
void updateTelemetryWiFiStatus() {
    static unsigned long lastRunMs = 0;
    static unsigned long lastSaveMs = 0;
    if(!isTimeForAction(&lastRunMs, 1 * 60 * 1000))
        return;

    g_wifiTracker->lastTestTime = millis();
    if(g_wifiTracker->clockSynced) {
        if(millis() - s_lastNtpSyncMs >= NTP_STALE_AFTER_MS) {
            // Kein Sync seit 24 h: NTP-Status zurücknehmen (TSF-/RDS-Fallback darf wieder greifen)
            g_wifiTracker->clockSynced = false;
            logEvent("NTP stale");
        } else if(isTimeForAction(&lastSaveMs, NTP_SAVE_INTERVAL_MS)) {
            saveLastKnownTime(time(NULL)); // Zeitstempel für Neustart-Restore sichern
        }
    }
}

// Startet den DNS-Server fürs Captive-Portal und den 1-min-Abschalttimer (nach dem AP-Start aufrufen)
static void startCaptivePortal() {
    dnsServer.start(53, "*", localIP);
    s_wifiStopTimerMs = millis();
    s_lastWebActivityMs = millis(); // Hotspot-Laufzeit (apTimeoutMin) beginnt mit dem Hotspot-Start
}

// Startet genau einen Modus; der andere bleibt, wie er ist (beide dürfen gleichzeitig laufen):
// withAP=true  → Hotspot (Weboberfläche erreichbar); läuft gerade das Heimnetz, wird es APSTA.
// withAP=false → Heimnetz für den NTP-Abgleich (nur mit eingetragenem WLAN); läuft der Hotspot, wird es APSTA.
// Läuft der gewünschte Modus schon, passiert nichts.
void startWifi(bool withAP) {
    const SystemSettings* s = g_systemSettings;
    wifi_mode_t mode = WiFi.getMode();
    bool apOn = (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA);
    bool staOn = (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA);

    if(withAP) {
        if(apOn)
            return;
        logEvent(staOn ? "Hotspot started (STA stays)" : "Hotspot started");
        WiFi.mode(staOn ? WIFI_MODE_APSTA : WIFI_MODE_AP);
        startAccessPoint(staOn ? "APSTA" : "AP");
        startCaptivePortal();
        serialPrint("AP aktiv — SSID: '" + String(s->internalSSID) + "'  IP: " + WiFi.softAPIP().toString()
                    + "  mode=" + String(WiFi.getMode()) + "  ch=" + String(s_wifiChannel));
    } else {
        if(staOn || strlen(s->externalSSID) == 0)
            return; // läuft schon bzw. kein Heimnetz eingetragen
        logEvent(apOn ? "STA started (sync, hotspot stays)" : "WiFi started (STA sync)");
        WiFi.mode(apOn ? WIFI_MODE_APSTA : WIFI_MODE_STA);
        beginSta(0); // Fallback-WLAN übernimmt handleStaDisconnected() nach 3 Fehlversuchen
    }

    // server.begin() nur beim ersten Mal aufrufen — AsyncWebServer unterstützt kein Neustart
    if(!serverStarted) {
        server.begin();
        serverStarted = true;
    }
}

// Schaltet nur den Hotspot ab (Drehknopf): bei APSTA bleibt das Heimnetz verbunden
void stopAccessPoint() {
    apOff("Hotspot off (user action)");
}

// Startet den Hotspot neu (z. B. nach geändertem Hotspot-Namen/-Passwort), nur wenn er gerade läuft;
// das Heimnetz bleibt unberührt
void restartAccessPoint() {
    wifi_mode_t mode = WiFi.getMode();
    if(mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)
        return;
    apOff("Hotspot restart (settings)");
    startWifi(true);
}

// Abgleich von Hand („Uhr jetzt synchronisieren“): Heimnetz verbinden, falls es nicht schon läuft —
// NTP startet dann bei GOT_IP. Ist es schon verbunden, nur die NTP-Abfrage neu starten.
// Der Hotspot bleibt an; die Oberfläche fragt das Ergebnis über /get_system_info (clock) ab.
void requestTimeSync() {
    if(g_wifiTracker->staConfigValid) {
        syncTimeWithNTP();
    } else {
        startWifi(false); // ohne eingetragenes Heimnetz passiert nichts
    }
}

// Startet einen neuen Abgleich mit den (geänderten) Heimnetz-Daten; der Hotspot bleibt unberührt
void restartStaSync() {
    staOff("STA restart (settings)");
    startWifi(false);
}

// Stoppt alle WiFi-Dienste (AP, STA, DNS)
void stopWifi() {
    serialPrint("stopWifi: Stopping all WiFi services");
    if(!isWiFiActive()) {
        serialPrint("WiFi is not active. Nothing to stop.");
        return;
    }
    logEvent("WiFi stopped");

    dnsServer.stop();
    WiFi.mode(WIFI_MODE_NULL);

    serialPrint("stopWifi: All WiFi services stopped");
}

// Blinkmuster der Status-LED je WLAN-Zustand: 0 = WLAN aus (langsam blinken), 1 = nur Heimnetz (STA),
// 2 = nur Hotspot (AP), 3 = Heimnetz und Hotspot (APSTA)
uint8_t wifiStatusBlinkCount() {
    switch(WiFi.getMode()) {
    case WIFI_MODE_STA:
        return 1;
    case WIFI_MODE_AP:
        return 2;
    case WIFI_MODE_APSTA:
        return 3;
    default:
        return 0;
    }
}

// Hotspot läuft, aber noch kein Gerät verbunden (Lampe zeigt dann die Warte-Animation)
bool isApWaitingForClient() {
    wifi_mode_t mode = WiFi.getMode();
    return (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA) && WiFi.softAPgetStationNum() == 0;
}

// Gibt true zurück wenn WiFi in irgendeinem Modus aktiv ist
bool isWiFiActive() {
    wifi_mode_t m = WiFi.getMode();
    return m == WIFI_MODE_AP || m == WIFI_MODE_APSTA || m == WIFI_MODE_STA;
}

// Gibt true zurück wenn mindestens ein Gerät mit dem eigenen Hotspot verbunden ist
bool hasWifiClients() {
    return WiFi.softAPgetStationNum() > 0;
}

// Setzt Zeitzone und — falls die Uhr noch leer ist (Einschalten/Reset) — die Systemzeit aus dem NVS.
// Nach Deep Sleep läuft die Uhr weiter und bleibt unverändert. Früh in setup() aufrufen, vor Weckern
// und WLAN, damit Weckzeiten von Anfang an in Ortszeit berechnet werden.
void initClock() {
    setenv("TZ", TIMEZONE, 1);
    tzset();
    if(time(nullptr) >= MIN_VALID_EPOCH) {
        return; // Uhr läuft bereits (z. B. nach Deep Sleep)
    }
    time_t saved = loadLastKnownTime();
    if(saved > 0) {
        struct timeval tv = {.tv_sec = saved, .tv_usec = 0};
        settimeofday(&tv, nullptr);
        serialPrint("Uhrzeit aus NVS wiederhergestellt: " + String((unsigned long)saved));
    } else {
        serialPrint("Keine Uhrzeit bekannt (noch nie synchronisiert)");
    }
}

// Merkt sich die aktuelle RTC-Zeit und startet SNTP-Sync neu (Drift wird im Callback berechnet)
void syncTimeWithNTP() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    s_preSyncMonoUs = esp_timer_get_time();
    // Nur eine plausible Uhrzeit taugt als Referenz für die Drift-Messung
    s_preSyncEpochUs = (tv.tv_sec >= MIN_VALID_EPOCH) ? ((int64_t)tv.tv_sec * 1000000LL + tv.tv_usec) : 0;
    sntp_restart();
}

// Verbindung verloren (läuft im Hauptthread): Wiederholungen, Fallback-WLAN, dann STA aufgeben.
// reason = Trennungsgrund des WLAN-Treibers (z. B. 201 = Netz nicht gefunden, 15 = Passwort falsch);
// steht im Protokoll, die Weboberfläche übersetzt ihn (logTranslations.js).
static void handleStaDisconnected(uint8_t reason) {
    // Während (und kurz nach) Suche/Test gehören Trennungen zum Test, nicht zur normalen Verbindung
    if(s_toolActive || (long)(millis() - s_toolQuietUntilMs) < 0)
        return;
    // Gescheiterter Versuch wird schon abgewickelt (TSF lauscht, danach Heimnetz aus)
    if(s_syncFailPending)
        return;
    // Kein Telemetrie-Update nötig: der NTP-Status hängt nicht mehr an der Verbindung
    char buf[40];
    snprintf(buf, sizeof(buf), "STA disconnected: reason %u", (unsigned)reason);
    logEvent(buf);
    serialPrint(buf);

    wifi_mode_t curMode = WiFi.getMode();
    if(curMode != WIFI_MODE_APSTA && curMode != WIFI_MODE_STA)
        return;

    g_wifiTracker->staConfigValid = false;
    if(s_retryCount < 3) {
        // Bis zu 3 direkte Wiederholungsversuche (der WiFi-Stack verbindet selbst neu)
        serialPrint("Retrying STA connection, attempt " + String(s_retryCount + 1));
        s_retryCount++;
        return;
    }
    s_retryCount = 0;

    // Nach 3 Fehlversuchen auf Fallback-WLAN wechseln (falls konfiguriert)
    if(s_staIdx == 0 && strlen(g_systemSettings->externalSSID2) > 0) {
        serialPrint("Primary failed, trying fallback WiFi.");
        beginSta(1);
        return;
    }

    s_staIdx = 0;
    syncFailed("STA failed: max retries"); // erst TSF-Fallback, dann Heimnetz aus (Hotspot bleibt)
}

// NTP-Versuch gescheitert (Fehlversuche oder Zeitlimit): Verbindungsversuch beenden, Funkteil aber
// anlassen, TSF-Fallback beauftragen (Uhrzeit aus den Bezugspunkten, neu gestartete APs neu basieren)
// und das Heimnetz erst danach abschalten (dropStaAfterFail). Ohne TSF geht es sofort aus.
static void syncFailed(const char* why) {
    logEvent(why);
    serialPrint(why);
    s_lastSyncOk = false;
    g_wifiTracker->staConfigValid = false;
    s_syncFailPending = true;
    WiFi.disconnect(false); // kein weiteres Verbinden, STA-Modus (Funkteil) bleibt für die Beacon-Erfassung
    requestTsfFallback();
}

// Schaltet nach gescheitertem Versuch das Heimnetz ab, sobald der TSF-Fallback fertig ist
static void dropStaAfterFail() {
    if(!s_syncFailPending || isTsfCaptureBusy())
        return;
    s_syncFailPending = false;
    staOff("STA off after failed sync");
}

// Trennt nach dem NTP-Abgleich die Heimnetz-Verbindung: APSTA → nur Hotspot, STA-only → WLAN aus.
// Wartet, bis der TSF-Snapshot (2 s Beacon-Erfassung auf dem aktuellen Kanal) und eine WLAN-Suche/-Test
// fertig sind, damit diesen nicht das Funkteil weggeschaltet wird.
static void dropStaAfterSync() {
    if(!s_dropStaPending || s_toolActive || isTsfCaptureBusy())
        return;
    s_dropStaPending = false;
    staOff(WiFi.getMode() == WIFI_MODE_APSTA ? "STA off after NTP, hotspot stays" : "STA off after NTP");
}

// Arbeitet die Nachrichten aus den Event-Handlern im Hauptthread ab
static void processWifiEvents() {
    WifiMsg m;
    while(s_msgQueue != nullptr && xQueueReceive(s_msgQueue, &m, 0) == pdTRUE) {
        switch(m.type) {
        case MSG_STA_START:
            s_retryCount = 0;
            break;
        case MSG_GOT_IP:
            if(s_toolActive)
                break; // Verbindung des Tests, nicht die gespeicherte
            // Verbindung erfolgreich → NTP-Sync sofort starten
            s_retryCount = 0;
            logEvent("STA connected, NTP sync");
            g_wifiTracker->staConfigValid = true;
            g_wifiTracker->lastStaConnectionTime = millis();
            syncTimeWithNTP();
            break;
        case MSG_DISCONNECTED:
            handleStaDisconnected((uint8_t)m.driftMs);
            break;
        case MSG_NTP_SYNCED:
            if(m.hasDrift) {
                char buf[52];
                snprintf(buf, sizeof(buf), "NTP sync: drift %+ldms", (long)m.driftMs);
                logEvent(buf);
                serialPrint(buf);
            } else {
                logEvent("NTP sync: no reference time");
            }
            // NTP-Status hier setzen (nicht per Polling): Zeitpunkt merken, Tracker aktualisieren, Zeit sichern
            s_lastNtpSyncMs = millis();
            if(!g_wifiTracker->clockSynced)
                logEvent("NTP sync successful");
            g_wifiTracker->clockSynced = true;
            s_lastSyncOk = true; // nächster Abgleich nach syncIntervalMin
            noteClockSync(CLOCK_SYNC_NTP); // für den Systemmonitor
            g_wifiTracker->lastSucceededTestTime = millis();
            saveLastKnownTime(time(NULL));
            onNtpSynced();
            s_dropStaPending = true; // Heimnetz nach dem TSF-Snapshot trennen (dropStaAfterSync)
            break;
        }
    }
}

// WiFi-Event-Handler (läuft im Event-Task!): nur ausgeben und Nachricht einreihen,
// keine WiFi-Aufrufe, kein Logging, keine Flash-Zugriffe
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    switch(event) {
    case ARDUINO_EVENT_WIFI_STA_STOP:
        serialPrint("STA Stop");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        serialPrint("STA Start");
        postMsg(MSG_STA_START);
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        serialPrint("STA Connected");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        serialPrint("STA Got IP");
        postMsg(MSG_GOT_IP);
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
        serialPrint("STA Disconnected");
        s_lastDisconnectReason = info.wifi_sta_disconnected.reason;
        postMsg(MSG_DISCONNECTED, false, info.wifi_sta_disconnected.reason);
        break;
    default:
        break;
    }
}
// ================================================================================================
// WLAN-Suche und Verbindungstest (Einstellungen → WLAN, Knöpfe „Finden“ und „Testen“)
// Webhandler legen nur einen Auftrag ab (request…), ausgeführt wird in wifiToolsLoop() im
// Hauptthread. Ergebnisse liegen unter Mutex und werden als JSON abgeholt (get…Json).
// Suche und Test brauchen die STA-Schnittstelle: im reinen Hotspot-Modus wird kurz auf AP+STA
// geschaltet und danach der vorherige Zustand (inkl. Heimnetz-Verbindung) wiederhergestellt.
// ================================================================================================

#define SCAN_MAX_RESULTS 20
#define SCAN_TIMEOUT_MS 15000
#define WIFI_TEST_TIMEOUT_MS 15000

enum ToolState : uint8_t { TOOL_IDLE, TOOL_REQUESTED, TOOL_RUNNING, TOOL_DONE };

struct ScanEntry {
    char ssid[33];
    int8_t rssi;
    bool secure;
};

static StaticSemaphore_t s_toolMutexBuf;
static SemaphoreHandle_t s_toolMutex = xSemaphoreCreateMutexStatic(&s_toolMutexBuf);

static ToolState s_scanState = TOOL_IDLE;
static bool s_scanFailed = false;
static ScanEntry s_scanResults[SCAN_MAX_RESULTS];
static uint8_t s_scanCount = 0;

static ToolState s_testState = TOOL_IDLE;
static char s_testSsid[33];
static char s_testPw[64];
static uint8_t s_testSlot = 1;          // 1 = externes WLAN, 2 = Fallback (für die Maske "******")
static const char* s_testResult = "none"; // ok | wrong_password | not_found | timeout | failed

static unsigned long s_toolStartMs = 0;
static wifi_mode_t s_modeBeforeTool = WIFI_MODE_NULL;
static bool s_staWasConnected = false;
static bool s_scanNeedsReconnect = false; // Heimnetz-Aufbau wurde für den Scan angehalten

// Auftrag: WLAN-Suche. false = gerade beschäftigt (Suche oder Test läuft)
bool requestWifiScan() {
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    bool busy = s_scanState == TOOL_REQUESTED || s_scanState == TOOL_RUNNING || s_testState == TOOL_REQUESTED
                || s_testState == TOOL_RUNNING;
    if(!busy)
        s_scanState = TOOL_REQUESTED;
    xSemaphoreGive(s_toolMutex);
    return !busy;
}

// Auftrag: Verbindungstest mit ssid/password; password "******" = gespeichertes Passwort von slot (1/2).
// false = gerade beschäftigt
bool requestWifiTest(uint8_t slot, const char* ssid, const char* password) {
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    bool busy = s_scanState == TOOL_REQUESTED || s_scanState == TOOL_RUNNING || s_testState == TOOL_REQUESTED
                || s_testState == TOOL_RUNNING;
    if(!busy) {
        strlcpy(s_testSsid, ssid, sizeof(s_testSsid));
        strlcpy(s_testPw, password, sizeof(s_testPw));
        s_testSlot = slot;
        s_testResult = "none";
        s_testState = TOOL_REQUESTED;
    }
    xSemaphoreGive(s_toolMutex);
    return !busy;
}

// {"status":"idle|running|done|failed","networks":[{"ssid","rssi","secure"}]} (stärkstes zuerst)
String getWifiScanJson() {
    JsonDocument doc;
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    const char* st = s_scanState == TOOL_DONE ? (s_scanFailed ? "failed" : "done")
                     : s_scanState == TOOL_IDLE ? "idle" : "running";
    doc["status"] = st;
    JsonArray arr = doc["networks"].to<JsonArray>();
    if(s_scanState == TOOL_DONE) {
        for(uint8_t i = 0; i < s_scanCount; i++) {
            JsonObject o = arr.add<JsonObject>();
            o["ssid"] = (const char*)s_scanResults[i].ssid;
            o["rssi"] = s_scanResults[i].rssi;
            o["secure"] = s_scanResults[i].secure;
        }
    }
    xSemaphoreGive(s_toolMutex);
    String out;
    serializeJson(doc, out);
    return out;
}

// {"status":"idle|running|done","result":"none|ok|wrong_password|not_found|timeout|failed","slot":1|2}
String getWifiTestJson() {
    JsonDocument doc;
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    doc["status"] = s_testState == TOOL_DONE ? "done" : s_testState == TOOL_IDLE ? "idle" : "running";
    doc["result"] = s_testResult;
    doc["slot"] = s_testSlot;
    xSemaphoreGive(s_toolMutex);
    String out;
    serializeJson(doc, out);
    return out;
}

// Vor Suche/Test: Zustand merken, normale STA-Logik pausieren, STA-Schnittstelle bereitstellen
static void beginTool() {
    s_toolActive = true;
    s_toolStartMs = millis();
    s_modeBeforeTool = WiFi.getMode();
    s_staWasConnected = WiFi.status() == WL_CONNECTED;
    if(s_modeBeforeTool == WIFI_MODE_AP)
        WiFi.mode(WIFI_MODE_APSTA);
    else if(s_modeBeforeTool == WIFI_MODE_NULL)
        WiFi.mode(WIFI_MODE_STA);
}

// Nach Suche/Test: vorherigen Zustand wiederherstellen; war das Heimnetz verbunden (oder im Aufbau),
// wird neu verbunden
static void endTool(bool staTouched) {
    if(staTouched)
        WiFi.disconnect(false); // nur STA trennen, Hotspot bleibt
    if(s_modeBeforeTool == WIFI_MODE_AP || s_modeBeforeTool == WIFI_MODE_NULL) {
        WiFi.mode(s_modeBeforeTool);
    } else if(staTouched && strlen(g_systemSettings->externalSSID) > 0) {
        g_wifiTracker->staConfigValid = false;
        s_retryCount = 0;
        beginSta(s_staIdx); // gespeichertes Netz wieder verbinden (GOT_IP setzt den Status)
    }
    s_toolQuietUntilMs = millis() + 1500;
    s_toolActive = false;
}

// Übernimmt die Scan-Ergebnisse: ohne leere SSIDs, doppelte Namen nur mit dem stärksten Signal,
// sortiert nach Signalstärke
static void collectScanResults(int found) {
    ScanEntry tmp[SCAN_MAX_RESULTS];
    uint8_t n = 0;
    for(int i = 0; i < found; i++) {
        String ssid = WiFi.SSID(i);
        if(ssid.isEmpty())
            continue;
        int8_t rssi = (int8_t)WiFi.RSSI(i);
        bool secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        int idx = -1;
        for(uint8_t k = 0; k < n; k++)
            if(strcmp(tmp[k].ssid, ssid.c_str()) == 0)
                idx = k;
        if(idx >= 0) {
            if(rssi > tmp[idx].rssi)
                tmp[idx].rssi = rssi;
            continue;
        }
        if(n < SCAN_MAX_RESULTS) {
            strlcpy(tmp[n].ssid, ssid.c_str(), sizeof(tmp[n].ssid));
            tmp[n].rssi = rssi;
            tmp[n].secure = secure;
            n++;
        } else {
            // Liste voll: schwächsten Eintrag ersetzen, falls dieses Netz stärker ist
            uint8_t weakest = 0;
            for(uint8_t k = 1; k < n; k++)
                if(tmp[k].rssi < tmp[weakest].rssi)
                    weakest = k;
            if(rssi > tmp[weakest].rssi) {
                strlcpy(tmp[weakest].ssid, ssid.c_str(), sizeof(tmp[weakest].ssid));
                tmp[weakest].rssi = rssi;
                tmp[weakest].secure = secure;
            }
        }
    }
    // Einfügesortierung absteigend nach RSSI (höchstens 20 Einträge)
    for(uint8_t i = 1; i < n; i++) {
        ScanEntry e = tmp[i];
        int j = i - 1;
        while(j >= 0 && tmp[j].rssi < e.rssi) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = e;
    }
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    memcpy(s_scanResults, tmp, sizeof(tmp));
    s_scanCount = n;
    s_scanFailed = false;
    s_scanState = TOOL_DONE;
    xSemaphoreGive(s_toolMutex);
}

// Setzt das Testergebnis und beendet den Test
static void finishTest(const char* result) {
    endTool(true);
    xSemaphoreTake(s_toolMutex, portMAX_DELAY);
    s_testResult = result;
    s_testState = TOOL_DONE;
    xSemaphoreGive(s_toolMutex);
    char buf[52];
    snprintf(buf, sizeof(buf), "WiFi test: %s", result);
    logEvent(buf);
}

// Hauptthread: führt Suche und Test schrittweise aus (blockiert nie)
static void wifiToolsLoop() {
    // --- Suche ---
    if(s_scanState == TOOL_REQUESTED) {
        beginTool();
        // Läuft gerade nur ein Verbindungsaufbau (nicht verbunden), schlägt der Scan fehl →
        // STA-Versuch anhalten und nach dem Scan neu verbinden
        s_scanNeedsReconnect = !s_staWasConnected && s_modeBeforeTool != WIFI_MODE_AP
                               && s_modeBeforeTool != WIFI_MODE_NULL;
        if(s_scanNeedsReconnect)
            WiFi.disconnect(false);
        int r = WiFi.scanNetworks(true, false); // asynchron, ohne versteckte Netze
        s_toolStartMs = millis();
        xSemaphoreTake(s_toolMutex, portMAX_DELAY);
        s_scanState = TOOL_RUNNING;
        xSemaphoreGive(s_toolMutex);
        if(r == WIFI_SCAN_FAILED)
            s_toolStartMs -= SCAN_TIMEOUT_MS; // im nächsten Durchlauf als fehlgeschlagen behandeln
        logEvent("WiFi scan started");
    } else if(s_scanState == TOOL_RUNNING) {
        int n = WiFi.scanComplete();
        if(n >= 0) {
            collectScanResults(n);
            WiFi.scanDelete();
            endTool(s_scanNeedsReconnect);
        } else if(n == WIFI_SCAN_FAILED || millis() - s_toolStartMs >= SCAN_TIMEOUT_MS) {
            WiFi.scanDelete();
            endTool(s_scanNeedsReconnect);
            xSemaphoreTake(s_toolMutex, portMAX_DELAY);
            s_scanCount = 0;
            s_scanFailed = true;
            s_scanState = TOOL_DONE;
            xSemaphoreGive(s_toolMutex);
            logEvent("WiFi scan failed");
        }
    }

    // --- Verbindungstest ---
    if(s_testState == TOOL_REQUESTED) {
        beginTool();
        // Maske = gespeichertes Passwort des Feldes
        const char* pw = s_testPw;
        if(strcmp(s_testPw, PASSWORD_MASK) == 0)
            pw = s_testSlot == 2 ? g_systemSettings->externalPW2 : g_systemSettings->externalPW;
        WiFi.disconnect(false);
        s_lastDisconnectReason = 0;
        WiFi.setTxPower(WIFI_TX_POWER);
        WiFi.begin(s_testSsid, pw[0] ? pw : nullptr);
        s_toolStartMs = millis();
        xSemaphoreTake(s_toolMutex, portMAX_DELAY);
        s_testState = TOOL_RUNNING;
        xSemaphoreGive(s_toolMutex);
        char buf[52];
        snprintf(buf, sizeof(buf), "WiFi test: %.36s", s_testSsid);
        logEvent(buf);
    } else if(s_testState == TOOL_RUNNING) {
        uint8_t reason = s_lastDisconnectReason;
        unsigned long elapsed = millis() - s_toolStartMs;
        if(WiFi.status() == WL_CONNECTED) {
            finishTest("ok");
        } else if(reason == WIFI_REASON_AUTH_FAIL || reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT
                  || reason == WIFI_REASON_HANDSHAKE_TIMEOUT || reason == WIFI_REASON_MIC_FAILURE) {
            finishTest("wrong_password");
        } else if(reason == WIFI_REASON_NO_AP_FOUND && elapsed >= 5000) {
            // Netz nicht gefunden: dem Stack ein paar Sekunden für weitere Versuche lassen
            finishTest("not_found");
        } else if(elapsed >= WIFI_TEST_TIMEOUT_MS) {
            finishTest(reason == WIFI_REASON_NO_AP_FOUND ? "not_found" : (reason ? "failed" : "timeout"));
        }
    }
}
