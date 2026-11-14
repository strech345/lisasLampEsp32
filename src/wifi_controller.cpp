#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "wifi_controller.h"
#include "route_handlers.h"
#include "types.h"
#include "system_utils.h"
#include "store.h"
#include "event_log.h"
#include "esp_sntp.h"

#define MAX_CLIENTS 4 // ESP32 supports up to 10 but I have not tested it yet
#define WIFI_CHANNEL 6
#define DNS_INTERVAL 30

static const IPAddress localIP(4, 3, 2, 1); // Samsung need to be in public space
static const IPAddress subnetMask(255, 255, 255, 0);
static const SystemSettings* g_systemSettings = nullptr;
static std::vector<Route> g_routes;
static WiFiTestTracker* g_wifiTracker = nullptr;
static uint8_t s_staIdx = 0; // 0 = primary, 1 = fallback

// Connect STA to the network at index idx (0=primary, 1=fallback).
// Falls back to primary if chosen network has no SSID.
static void beginSta(uint8_t idx) {
    const char* ssid = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0)
                           ? g_systemSettings->externalSSID2
                           : g_systemSettings->externalSSID;
    const char* pw   = (idx == 1 && strlen(g_systemSettings->externalSSID2) > 0)
                           ? g_systemSettings->externalPW2
                           : g_systemSettings->externalPW;
    s_staIdx = (ssid == g_systemSettings->externalSSID2) ? 1 : 0;
    char buf[52];
    snprintf(buf, sizeof(buf), "STA connect%s: %.30s", s_staIdx ? " (fallback)" : "", ssid);
    logEvent(buf);
    WiFi.begin(ssid, pw);
}

DNSServer dnsServer;
AsyncWebServer server(80);
static bool serverStarted = false;

void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker) {
    Serial.println("Initializing WiFi controller");
    Serial.printf("Internal SSID: %s\n", settings.internalSSID);
    Serial.printf("External SSID: %s\n", settings.externalSSID);

    g_systemSettings = &settings;
    g_routes = routes;
    g_wifiTracker = &tracker;

    configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
    WiFi.onEvent(WiFiEvent);
    // Moved to startWifi for correct ordering
    // WiFi.softAPConfig(localIP, localIP, subnetMask); 
    WiFi.mode(WIFI_MODE_NULL);

    setUpDNSServer(dnsServer, localIP);
    setUpWebserver(server, localIP, g_routes);
    registerOtaRoute(server);

    Serial.println("WiFi controller initialized");
}

void wifiLoop() {
    handleClientRequests();
    checkWifiStart();
    checkWifiStop();
    updateTelemetryWiFiStatus();
    tryReconnectSta();
}

void handleClientRequests() {
    static unsigned long lastDnsProcessMs = 0;
    if(!isTimeForAction(&lastDnsProcessMs, DNS_INTERVAL))
        return;

    // Process DNS requests if we're in AP or APSTA mode
    wifi_mode_t currentMode = WiFi.getMode();
    if(currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA) {
        dnsServer.processNextRequest();
    }
}

void checkWifiStop() {
    static unsigned long lastRunMs = 0;
    if(!isWiFiActive() || hasWifiClients()) {
        lastRunMs = millis();
        return;
    }

    if(isTimeForAction(&lastRunMs, STOP_WIFI_AFTER_MS)) {
        stopWifi();
        lastRunMs = 0;
    }
}

void checkWifiStart() {
    static unsigned long lastRunMs = 0;
    if(isWiFiActive()) {
        lastRunMs = millis();
        return;
    }

    if(isTimeForAction(&lastRunMs, START_WIFI_AFTER_MS)) {
        startWifi(false); // STA only — no AP broadcast for periodic NTP sync
        lastRunMs = 0;
    }
}

void updateTelemetryWiFiStatus() {
    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 1 * 60 * 1000)) // every 1 minute
        return;

    bool wasSynced = g_wifiTracker->clockSynced;
    g_wifiTracker->clockSynced = isTimeSyncedWithNTP();
    g_wifiTracker->lastTestTime = millis();
    if(g_wifiTracker->clockSynced) {
        g_wifiTracker->lastSucceededTestTime = millis();
        saveLastKnownTime(time(NULL));
        if(!wasSynced) logEvent("NTP sync successful");
    } else {
        if(wasSynced) logEvent("NTP sync lost");
    }
    Serial.println("Updated telemetry " + String(g_wifiTracker->clockSynced ? "synced" : "not synced"));
}

void tryReconnectSta() {
    if(g_wifiTracker->staConfigValid) {
        return;
    }
    if(strlen(g_systemSettings->externalSSID) == 0) {
        return;
    }

    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 5 * 60 * 1000)) // every 5 minutes
        return;

    // Alternate between primary and fallback on each retry
    uint8_t nextIdx = (strlen(g_systemSettings->externalSSID2) > 0) ? (1 - s_staIdx) : 0;

    wifi_mode_t mode = WiFi.getMode();
    if(mode == WIFI_MODE_APSTA) {
        Serial.println("tryReconnectSta: reconnecting STA...");
        beginSta(nextIdx);
    } else if(mode == WIFI_MODE_AP) {
        // AP is running (user-visible) → restore STA alongside it
        Serial.println("tryReconnectSta: restoring STA in APSTA mode...");
        WiFi.mode(WIFI_MODE_APSTA);
        beginSta(nextIdx);
    } else if(mode == WIFI_MODE_NULL) {
        // STA-only was stopped after failure → restart STA-only
        Serial.println("tryReconnectSta: restarting STA-only...");
        WiFi.mode(WIFI_MODE_STA);
        beginSta(nextIdx);
    }
}

void startWifi(bool withAP) {
    const SystemSettings* s = g_systemSettings;

    if(isWiFiActive()) {
        // Already running — upgrade STA-only to APSTA if user now wants the AP.
        if(withAP && WiFi.getMode() == WIFI_MODE_STA) {
            Serial.println("Upgrading STA → APSTA");
            WiFi.mode(WIFI_MODE_APSTA);
            WiFi.softAPConfig(localIP, localIP, subnetMask);
            WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
            dnsServer.start(53, "*", localIP);
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
        // No external SSID — AP-only or nothing
        if(!withAP) return;
        WiFi.mode(WIFI_MODE_AP);
        WiFi.softAPConfig(localIP, localIP, subnetMask);
        WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
    }

    if(!serverStarted) {
        server.begin();
        serverStarted = true;
    }
    if(withAP) {
        dnsServer.start(53, "*", localIP);
        Serial.println("AP IP address: " + WiFi.softAPIP().toString());
    }
}

void stopWifi() {
    Serial.println("stopWifi: Stopping all WiFi services");
    if(!isWiFiActive()) {
        Serial.println("WiFi is not active. Nothing to stop.");
        return;
    }
    logEvent("WiFi stopped");

    dnsServer.stop();
    WiFi.mode(WIFI_MODE_NULL);

    Serial.println("stopWifi: All WiFi services stopped");
}

bool isWiFiActive() {
    wifi_mode_t m = WiFi.getMode();
    return m == WIFI_MODE_AP || m == WIFI_MODE_APSTA || m == WIFI_MODE_STA;
}

bool hasWifiClients() {
    return WiFi.softAPgetStationNum() > 0;
}

void syncTimeWithNTP() {
    sntp_restart(); // sntp_init();
}

bool isTimeSyncedWithNTP() {
    return (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED);
}

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    static int retryCount = 0;
    switch(event) {
    case ARDUINO_EVENT_WIFI_STA_STOP:
        Serial.println("STA Stop");
        break;
    case ARDUINO_EVENT_WIFI_STA_START:
        Serial.println("STA Start");
        retryCount = 0;
        break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
        Serial.println("STA Connected");
        break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
        Serial.println("STA Got IP");
        logEvent("STA connected, NTP sync");
        g_wifiTracker->staConfigValid = true;
        g_wifiTracker->lastStaConnectionTime = millis();
        syncTimeWithNTP();
        updateTelemetryWiFiStatus();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        updateTelemetryWiFiStatus();
        Serial.println("STA Disconnected");
        logEvent("STA disconnected");

        {
            wifi_mode_t curMode = WiFi.getMode();
            if(curMode == WIFI_MODE_APSTA || curMode == WIFI_MODE_STA) {
                g_wifiTracker->staConfigValid = false;
                if(retryCount < 3) {
                    Serial.println("Retrying STA connection, attempt " + String(retryCount + 1));
                    retryCount++;
                    return;
                }
                retryCount = 0;
                // Try fallback network immediately if available and on primary.
                if(s_staIdx == 0 && strlen(g_systemSettings->externalSSID2) > 0) {
                    Serial.println("Primary failed, trying fallback WiFi.");
                    beginSta(1);
                } else {
                    s_staIdx = 0;
                    if(curMode == WIFI_MODE_APSTA) {
                        WiFi.mode(WIFI_MODE_AP); // keep AP running
                        Serial.println("Max retries. Keeping AP, dropping STA.");
                    } else {
                        WiFi.mode(WIFI_MODE_NULL); // STA-only: stop entirely
                        Serial.println("Max retries. Stopping STA-only WiFi.");
                    }
                }
            }
        }
        break;
    }
    }
}

void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP) {
    // Set the TTL for DNS response and start the DNS server
    dnsServer.setTTL(3600);
    // Ensure the DNS reply code is NoError so client OSes treat DNS replies
    // like valid responses
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
}

void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes) {
    // https://github.com/CDFER/Captive-Portal-ESP32/blob/main/src/main.cpp
    //  Captive Portal
    // Pre reading on the fundamentals of captive portals
    // https://textslashplain.com/2022/06/24/captive-portals/
    // Initialize the runtime URL used by redirect handlers so it's always
    // correct
    const String localIPURL = String("http://") + localIP.toString();

    //======================== Webserver ========================
    // WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD
    // "Success" https://www.esp8266.com/viewtopic.php?f=34&t=4398 SAFARI (IOS)
    // IS STUPID, G-ZIPPED FILES CAN'T END IN .GZ
    // https://github.com/homieiot/homie-esp8266/issues/476 this is fixed by the
    // webserver serve static function. SAFARI (IOS) there is a 128KB limit to
    // the size of the HTML. The HTML can reference external resources/images
    // that bring the total over 128KB SAFARI (IOS) popup browser has some
    // severe limitations (javascript disabled, cookies disabled)

    // Required
    server.on("/connecttest.txt", [](AsyncWebServerRequest* request) {
        request->redirect("http://logout.net");
    }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest* request) {
        request->send(404);
    }); // Honestly don't understand what this is but a 404 stops win 10 keep
        // calling this repeatedly and panicking the esp32 :)

    // Background responses: Probably not all are Required, but some are. Others
    // might speed things up? A Tier (commonly used by modern systems)
    server.on("/generate_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // android captive portal redirect
    server.on("/gen_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // android captive portal redirect (alternative)
    server.on("/redirect",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // microsoft redirect
    server.on("/hotspot-detect.html",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // apple call home
    server.on("/canonical.html", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // firefox captive portal call home
    server.on("/success.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // windows call home

    // B Tier (uncommon)
    //  server.on("/chrome-variations/seed",[](AsyncWebServerRequest
    //  *request){request->send(200);}); //chrome captive portal call home
    //  server.on("/service/update2/json",[](AsyncWebServerRequest
    //  *request){request->send(200);});
    //  //firefox? server.on("/chat",[](AsyncWebServerRequest
    //  *request){request->send(404);}); //No stop asking Whatsapp, there is no
    //  internet connection server.on("/startpage",[](AsyncWebServerRequest
    //  *request){request->redirect(localIPURL);});

    // return 404 to webpage icon
    server.on("/favicon.ico", [](AsyncWebServerRequest* request) { request->send(404); }); // webpage icon

    // Register provided application routes
    for(const auto& r : routes) {
        if(r.method == HTTP_POST) {
            // For POST routes, capture body and pass to handler
            server.on(
                r.uri, r.method,
                [](AsyncWebServerRequest* request) {
                    // Do nothing in the initial request handler for POST
                    // The body handler will trigger the actual logic
                },
                NULL,
                [r](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if(index == 0) {
                        // Start of body, initialize buffer
                        request->_tempObject = new String("");
                        if(request->_tempObject == nullptr) {
                            request->send(500, "text/plain", "Buffer Allocation Failed");
                            return;
                        }
                    }

                    String* body = (String*)request->_tempObject;
                    if(body) {
                        // Append this chunk efficiently
                        body->concat((const char*)data, len);
                    }

                    if(index + len == total) {
                        // Full body received
                        if(body) {
                            r.handler(request, *body);
                            delete body;
                            request->_tempObject = nullptr;
                        } else {
                            request->send(400, "text/plain", "Bad Request: Body lost");
                        }
                    } else if(request->client() && !request->client()->connected()) {
                        // Client disconnected before full body received, clean up
                        if(body) {
                            delete body;
                            request->_tempObject = nullptr;
                        }
                    }
                });
        } else {
            // For GET routes, pass empty body
            server.on(r.uri, r.method, [r](AsyncWebServerRequest* request) { r.handler(request, String("")); });
        }
    }

    // the catch all
    server.onNotFound([localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
        Serial.printf("onnotfound [Host: %s][URL: %s] -> redirect to %s\n", 
                     request->host().c_str(), request->url().c_str(), localIPURL.c_str());
    });
}
