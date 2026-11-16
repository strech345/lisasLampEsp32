#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <esp_task_wdt.h>
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
#define WIFI_CHANNEL 6
#define DNS_INTERVAL 30 // DNS-Anfragen alle 30ms verarbeiten

// Feste IP für den eigenen Hotspot (AP-Modus) — Samsung-Geräte brauchen eine öffentliche IP
static const IPAddress localIP(4, 3, 2, 1);
static const IPAddress subnetMask(255, 255, 255, 0);
static const SystemSettings* g_systemSettings = nullptr;
static std::vector<Route> g_routes;
static WiFiTestTracker* g_wifiTracker = nullptr;
static uint8_t s_staIdx = 0; // 0 = primäres WLAN, 1 = Fallback-WLAN

// Verbindet mit dem externen WLAN an Index idx; fällt auf primäres zurück wenn kein Fallback konfiguriert
static void beginSta(uint8_t idx) {
    const char* ssid = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0)
                           ? g_systemSettings->externalSSID2
                           : g_systemSettings->externalSSID;
    const char* pw   = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0)
                           ? g_systemSettings->externalPW2
                           : g_systemSettings->externalPW;
    // Zeigervergleich absichtlich: ssid zeigt direkt in g_systemSettings
    s_staIdx = (ssid == g_systemSettings->externalSSID2) ? 1 : 0;
    char buf[52];
    snprintf(buf, sizeof(buf), "STA connect%s: %.30s", s_staIdx ? " (fallback)" : "", ssid);
    logEvent(buf);
    WiFi.begin(ssid, pw);
}

DNSServer dnsServer;
AsyncWebServer server(80);
static bool serverStarted = false; // server.begin() darf nur einmal aufgerufen werden

// RTC-Zeit kurz vor sntp_restart() — zum Berechnen des Zeitdrifts nach der Synchronisation
static struct timeval s_preSyncTime = {0, 0};

// Callback von der SNTP-Bibliothek nach erfolgreicher Zeitsynchonisation
static void ntpSyncCallback(struct timeval* tv) {
    if(tv == nullptr)
        return;
    if(s_preSyncTime.tv_sec > 0) {
        long driftMs = ((long)tv->tv_sec - (long)s_preSyncTime.tv_sec) * 1000L
                       + ((long)tv->tv_usec - (long)s_preSyncTime.tv_usec) / 1000L;
        char buf[52];
        snprintf(buf, sizeof(buf), "NTP sync: Abweichung %+ldms", driftMs);
        logEvent(buf);
        serialPrint(buf);
    } else {
        logEvent("NTP sync: erste Synchronisation");
    }
    s_preSyncTime = {0, 0};
    onNtpSynced();
}

// Initialisiert den WiFi-Controller einmalig: Zeitzone, DNS, Webserver, Event-Handler
void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker) {
    serialPrint("Initializing WiFi controller");
    serialPrint(String("Internal SSID: ") + settings.internalSSID);
    serialPrint(String("External SSID: ") + settings.externalSSID);

    g_systemSettings = &settings;
    g_routes = routes;
    g_wifiTracker = &tracker;

    // Zeitzone: Mitteleuropäische Zeit mit Sommerzeit
    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    sntp_set_time_sync_notification_cb(ntpSyncCallback);
    WiFi.onEvent(WiFiEvent);
    WiFi.mode(WIFI_MODE_NULL);

    setUpDNSServer(dnsServer, localIP);
    setUpWebserver(server, localIP, g_routes);
    registerOtaRoute(server);

    serialPrint("WiFi controller initialized");
}

// Läuft in der Hauptschleife: DNS-Anfragen, Auto-Start/Stop, Telemetrie, Reconnect
void wifiLoop() {
    handleClientRequests();
    checkWifiStart();
    checkWifiStop();
    updateTelemetryWiFiStatus();
    tryReconnectSta();
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

// Schaltet WiFi ab wenn 60 Sekunden lang kein Client verbunden war
void checkWifiStop() {
    if(!isWiFiActive() || hasWifiClients()) {
        s_wifiStopTimerMs = millis();
        return;
    }
    if(isTimeForAction(&s_wifiStopTimerMs, STOP_WIFI_AFTER_MS)) {
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

// Aktualisiert den NTP-Sync-Status einmal pro Minute und speichert die Zeit in NVS
void updateTelemetryWiFiStatus() {
    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 1 * 60 * 1000))
        return;

    bool wasSynced = g_wifiTracker->clockSynced;
    g_wifiTracker->clockSynced = isTimeSyncedWithNTP();
    g_wifiTracker->lastTestTime = millis();
    if(g_wifiTracker->clockSynced) {
        g_wifiTracker->lastSucceededTestTime = millis();
        saveLastKnownTime(time(NULL)); // Zeitstempel für Neustart-Restore sichern
        if(!wasSynced) logEvent("NTP sync successful");
    } else {
        if(wasSynced) logEvent("NTP sync lost");
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

// Startet WiFi: withAP=true → APSTA (Nutzer-sichtbar, Weboberfläche erreichbar),
// withAP=false → nur STA für automatischen NTP-Sync ohne sichtbaren Hotspot
void startWifi(bool withAP) {
    const SystemSettings* s = g_systemSettings;

    if(isWiFiActive()) {
        // Bereits aktiv — bei Bedarf von STA-only auf APSTA hochstufen
        if(withAP && WiFi.getMode() == WIFI_MODE_STA) {
            serialPrint("Upgrading STA → APSTA");
            WiFi.mode(WIFI_MODE_APSTA);
            WiFi.softAPConfig(localIP, localIP, subnetMask);
            WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
            dnsServer.start(53, "*", localIP);
            s_wifiStopTimerMs = millis();
            logEvent("AP activated (upgrade)");
        }
        return;
    }

    logEvent(withAP ? "WiFi started (AP+STA)" : "WiFi started (STA sync)");

    if(strlen(s->externalSSID) > 0) {
        if(withAP) {
            WiFi.mode(WIFI_MODE_APSTA);
            WiFi.softAPConfig(localIP, localIP, subnetMask);
            WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
        } else {
            WiFi.mode(WIFI_MODE_STA);
        }
        beginSta(0);
    } else {
        // Kein externes WLAN konfiguriert → nur AP oder gar nicht
        if(!withAP) return;
        WiFi.mode(WIFI_MODE_AP);
        WiFi.softAPConfig(localIP, localIP, subnetMask);
        WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
    }

    // server.begin() nur beim ersten Mal aufrufen — AsyncWebServer unterstützt kein Neustart
    if(!serverStarted) {
        server.begin();
        serverStarted = true;
    }
    if(withAP) {
        dnsServer.start(53, "*", localIP);
        s_wifiStopTimerMs = millis();
        serialPrint("AP IP address: " + WiFi.softAPIP().toString());
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

// Speichert aktuelle RTC-Zeit und startet SNTP-Sync neu (Drift wird im Callback berechnet)
void syncTimeWithNTP() {
    gettimeofday(&s_preSyncTime, nullptr);
    sntp_restart();
}

bool isTimeSyncedWithNTP() {
    return (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

// WiFi-Event-Handler: reagiert auf Verbindungsaufbau, IP-Erhalt und Verbindungsabbrüche
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    static int retryCount = 0;
    switch(event) {
    case ARDUINO_EVENT_WIFI_STA_STOP:
        serialPrint("STA Stop");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        serialPrint("STA Start");
        retryCount = 0;
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        serialPrint("STA Connected");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        // Verbindung erfolgreich → NTP-Sync sofort starten
        serialPrint("STA Got IP");
        logEvent("STA connected, NTP sync");
        g_wifiTracker->staConfigValid = true;
        g_wifiTracker->lastStaConnectionTime = millis();
        syncTimeWithNTP();
        updateTelemetryWiFiStatus();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        updateTelemetryWiFiStatus();
        serialPrint("STA Disconnected");
        logEvent("STA disconnected");

        {
            wifi_mode_t curMode = WiFi.getMode();
            if(curMode == WIFI_MODE_APSTA || curMode == WIFI_MODE_STA) {
                g_wifiTracker->staConfigValid = false;
                if(retryCount < 3) {
                    // Bis zu 3 direkte Wiederholungsversuche
                    serialPrint("Retrying STA connection, attempt " + String(retryCount + 1));
                    retryCount++;
                    return;
                }
                retryCount = 0;
                // Nach 3 Fehlversuchen auf Fallback-WLAN wechseln (falls konfiguriert)
                if(s_staIdx == 0 && strlen(g_systemSettings->externalSSID2) > 0) {
                    serialPrint("Primary failed, trying fallback WiFi.");
                    beginSta(1);
                } else {
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
            }
        }
        break;
    }
    }
}
