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

#define MAX_CLIENTS 4
#define WIFI_AP_TX_POWER WIFI_POWER_17dBm
#define DNS_INTERVAL 30             // DNS-Anfragen alle 30ms verarbeiten
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

// Verbindet mit dem externen WLAN an Index idx; fällt auf primäres zurück wenn kein Fallback konfiguriert
static void beginSta(uint8_t idx) {
    bool useFallback = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0);
    const char* ssid = useFallback ? g_systemSettings->externalSSID2 : g_systemSettings->externalSSID;
    const char* pw = useFallback ? g_systemSettings->externalPW2 : g_systemSettings->externalPW;
    s_staIdx = useFallback ? 1 : 0;
    char buf[52]; // passt zu EVENT_MSG_LEN: 24 Zeichen Präfix + 27 SSID-Zeichen
    snprintf(buf, sizeof(buf), "STA connect%s: %.27s", useFallback ? " (fallback)" : "", ssid);
    logEvent(buf);
    WiFi.begin(ssid, pw);
}

// Startet den eigenen Hotspot (WiFi-Modus AP oder APSTA muss schon gesetzt sein)
static bool startAccessPoint(const char* tag) {
    const SystemSettings* s = g_systemSettings;
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT40);
    WiFi.setTxPower(WIFI_AP_TX_POWER);
    delay(100);
    serialPrint("setTxPower=" + String(WiFi.getTxPower()) + " target=" + String(WIFI_AP_TX_POWER));
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
    int32_t driftMs;
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
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
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
    handleClientRequests();
    checkWifiStart();
    checkWifiStop();
    updateTelemetryWiFiStatus();
    tryReconnectSta();

    static unsigned long lastStatusMs = 0;
    if(isTimeForAction(&lastStatusMs, 10000)) {
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

// Schaltet WiFi ab wenn STOP_WIFI_AFTER_MS (1min) lang kein Client verbunden war
void checkWifiStop() {
    if(!isWiFiActive() || hasWifiClients()) {
        s_wifiStopTimerMs = millis();
        return;
    }
    if(isTimeForAction(&s_wifiStopTimerMs, STOP_WIFI_AFTER_MS)) {
        serialPrint("checkWifiStop: 1min ohne Client — stoppe AP");
        stopWifi();
        s_wifiStopTimerMs = 0;
    }
}

// Startet WiFi automatisch alle 5 Stunden für eine NTP-Zeitsynchronisation (ohne AP)
void checkWifiStart() {
    static unsigned long lastRunMs = 0;
    if(isWiFiActive()) {
        lastRunMs = millis();
        return;
    }

    if(isTimeForAction(&lastRunMs, START_WIFI_AFTER_MS)) {
        startWifi(false); // Nur STA — kein AP-Hotspot für automatische Sync
        lastRunMs = 0;
    }
}

// Zeitpunkt (millis) des letzten erfolgreichen NTP-Syncs; nur im Hauptthread (processWifiEvents) gesetzt.
// Eigenes Flag, weil sntp_get_sync_status() COMPLETED nur einmal meldet und danach auf RESET fällt.
static unsigned long s_lastNtpSyncMs = 0;

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
            logEvent("NTP veraltet");
        } else if(isTimeForAction(&lastSaveMs, NTP_SAVE_INTERVAL_MS)) {
            saveLastKnownTime(time(NULL)); // Zeitstempel für Neustart-Restore sichern
        }
    }
    serialPrint("Updated telemetry " + String(g_wifiTracker->clockSynced ? "synced" : "not synced"));
}

// Versucht alle 5 Minuten eine neue STA-Verbindung wenn die letzte fehlgeschlagen ist
// Wechselt abwechselnd zwischen primärem und Fallback-WLAN
void tryReconnectSta() {
    if(g_wifiTracker->staConfigValid) {
        return;
    }
    if(strlen(g_systemSettings->externalSSID) == 0) {
        return;
    }

    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 5 * 60 * 1000))
        return;

    // Bei jedem Versuch zwischen primärem und Fallback wechseln
    uint8_t nextIdx = (strlen(g_systemSettings->externalSSID2) > 0) ? (1 - s_staIdx) : 0;

    wifi_mode_t mode = WiFi.getMode();
    if(mode == WIFI_MODE_APSTA) {
        serialPrint("tryReconnectSta: reconnecting STA...");
        beginSta(nextIdx);
    } else if(mode == WIFI_MODE_AP) {
        // AP läuft (Nutzer-sichtbar) → STA zusätzlich starten
        serialPrint("tryReconnectSta: restoring STA in APSTA mode...");
        WiFi.mode(WIFI_MODE_APSTA);
        beginSta(nextIdx);
    } else if(mode == WIFI_MODE_NULL) {
        // STA-only wurde nach Fehler gestoppt → neu starten
        serialPrint("tryReconnectSta: restarting STA-only...");
        WiFi.mode(WIFI_MODE_STA);
        beginSta(nextIdx);
    }
}

// Startet den DNS-Server fürs Captive-Portal und den 1-min-Abschalttimer (nach dem AP-Start aufrufen)
static void startCaptivePortal() {
    dnsServer.start(53, "*", localIP);
    s_wifiStopTimerMs = millis();
}

// Startet WiFi: withAP=true → APSTA (Nutzer-sichtbar, Weboberfläche erreichbar),
// withAP=false → nur STA für automatischen NTP-Sync ohne sichtbaren Hotspot
void startWifi(bool withAP) {
    const SystemSettings* s = g_systemSettings;

    if(isWiFiActive()) {
        // Bereits aktiv — bei Bedarf von STA-only auf APSTA hochstufen
        if(withAP && WiFi.getMode() == WIFI_MODE_STA) {
            serialPrint("Upgrading STA → APSTA");
            WiFi.mode(WIFI_MODE_APSTA);
            startAccessPoint("upgrade");
            startCaptivePortal();
            logEvent("AP activated (upgrade)");
        }
        return;
    }

    bool hasExternal = strlen(s->externalSSID) > 0;
    // Kein externes WLAN und kein AP gewünscht → es gibt nichts zu starten
    if(!hasExternal && !withAP)
        return;

    logEvent(withAP ? "WiFi started (AP+STA)" : "WiFi started (STA sync)");

    if(withAP) {
        WiFi.mode(hasExternal ? WIFI_MODE_APSTA : WIFI_MODE_AP);
        startAccessPoint(hasExternal ? "APSTA" : "AP");
    } else {
        WiFi.mode(WIFI_MODE_STA);
    }
    if(hasExternal)
        beginSta(0);

    // server.begin() nur beim ersten Mal aufrufen — AsyncWebServer unterstützt kein Neustart
    if(!serverStarted) {
        server.begin();
        serverStarted = true;
    }
    if(withAP) {
        startCaptivePortal();
        wifi_mode_t actualMode = WiFi.getMode();
        serialPrint("AP aktiv — SSID: '" + String(s->internalSSID) + "'  IP: " + WiFi.softAPIP().toString()
                    + "  mode=" + String(actualMode) + "  ch=" + String(s_wifiChannel));
    }
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

// Gibt true zurück wenn WiFi in irgendeinem Modus aktiv ist
bool isWiFiActive() {
    wifi_mode_t m = WiFi.getMode();
    return m == WIFI_MODE_AP || m == WIFI_MODE_APSTA || m == WIFI_MODE_STA;
}

// Gibt true zurück wenn mindestens ein Gerät mit dem eigenen Hotspot verbunden ist
bool hasWifiClients() {
    return WiFi.softAPgetStationNum() > 0;
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

// Verbindung verloren (läuft im Hauptthread): Wiederholungen, Fallback-WLAN, dann STA aufgeben
static void handleStaDisconnected() {
    // Kein Telemetrie-Update nötig: der NTP-Status hängt nicht mehr an der Verbindung
    logEvent("STA disconnected");

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
    if(curMode == WIFI_MODE_APSTA) {
        // AP weiter lassen, STA aufgeben
        WiFi.mode(WIFI_MODE_AP);
        serialPrint("Max retries. Keeping AP, dropping STA.");
    } else {
        // Kein AP aktiv → alles stoppen
        WiFi.mode(WIFI_MODE_NULL);
        serialPrint("Max retries. Stopping STA-only WiFi.");
    }
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
            // Verbindung erfolgreich → NTP-Sync sofort starten
            s_retryCount = 0;
            logEvent("STA connected, NTP sync");
            g_wifiTracker->staConfigValid = true;
            g_wifiTracker->lastStaConnectionTime = millis();
            syncTimeWithNTP();
            break;
        case MSG_DISCONNECTED:
            handleStaDisconnected();
            break;
        case MSG_NTP_SYNCED:
            if(m.hasDrift) {
                char buf[52];
                snprintf(buf, sizeof(buf), "NTP sync: Abweichung %+ldms", (long)m.driftMs);
                logEvent(buf);
                serialPrint(buf);
            } else {
                logEvent("NTP sync: ohne Referenzzeit");
            }
            // NTP-Status hier setzen (nicht per Polling): Zeitpunkt merken, Tracker aktualisieren, Zeit sichern
            s_lastNtpSyncMs = millis();
            if(!g_wifiTracker->clockSynced)
                logEvent("NTP sync successful");
            g_wifiTracker->clockSynced = true;
            g_wifiTracker->lastSucceededTestTime = millis();
            saveLastKnownTime(time(NULL));
            onNtpSynced();
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
        postMsg(MSG_DISCONNECTED);
        break;
    default:
        break;
    }
}