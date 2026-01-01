#include <Arduino.h>
#include <AsyncTCP.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>
#include <esp_task_wdt.h>
#include "wifi_controller.h"
#include "types.h"
#include "system_utils.h"
#include "esp_sntp.h"

#define MAX_CLIENTS 4 // ESP32 supports up to 10 but I have not tested it yet
#define WIFI_CHANNEL 6
#define DNS_INTERVAL 30

static const unsigned long nextTestIntervalOnSuccess = 12 * 60 * 60 * 1000; // 12 hours
static const unsigned long nextTestIntervalOnFailure = 2 * 60 * 1000;       // 2 minutes
static const unsigned long WIFI_IDLE_TIMEOUT = 100 * 30 * 1000;             // 60 seconds
static const unsigned long WIFI_IDLE_TIME = 1 * 60 * 1000;                  // 5 minutes max
static const IPAddress localIP(4, 3, 2, 1);                                 // Samsung need to be in public space
static const IPAddress subnetMask(255, 255, 255, 0);
static const SystemSettings* g_systemSettings = nullptr;
static std::vector<Route> g_routes;
static WiFiTestTracker* g_wifiTracker = nullptr;

DNSServer dnsServer;
AsyncWebServer server(80);

void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker) {
    Serial.println("Initializing WiFi controller");
    Serial.printf("Internal SSID: %s\n", settings.internalSSID);
    Serial.printf("External SSID: %s\n", settings.externalSSID);

    g_systemSettings = &settings;
    g_routes = routes;
    g_wifiTracker = &tracker;

    configTime(3600, 3600, "pool.ntp.org", "time.nist.gov");
    WiFi.onEvent(WiFiEvent);
    WiFi.softAPConfig(localIP, localIP, subnetMask); // will change the mode
    WiFi.mode(WIFI_MODE_NULL);

    setUpDNSServer(dnsServer, localIP);
    setUpWebserver(server, localIP, g_routes);

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
    if(!isWiFiActive() || WiFi.softAPgetStationNum() > 0) {
        lastRunMs = millis();
        return;
    }

    if(isTimeForAction(&lastRunMs, WIFI_IDLE_TIMEOUT)) {
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

    if(isTimeForAction(&lastRunMs, WIFI_IDLE_TIME)) {
        startWifi();
        lastRunMs = 0;
    }
}

void updateTelemetryWiFiStatus() {
    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 1 * 60 * 1000)) // every 1 minute
        return;

    g_wifiTracker->clockSynced = isTimeSyncedWithNTP();
    g_wifiTracker->lastTestTime = millis();
    if(g_wifiTracker->clockSynced) {
        g_wifiTracker->lastSucceededTestTime = millis();
    }
    Serial.println("Updated telemetry " + String(g_wifiTracker->clockSynced ? "synced" : "not synced"));
}

void tryReconnectSta() {
    if(g_wifiTracker->staConfigValid || WiFi.getMode() != WIFI_MODE_APSTA) {
        return;
    }

    static unsigned long lastRunMs = 0;
    if(!isTimeForAction(&lastRunMs, 10 * 1000)) // every 10 seconds
        return;

    Serial.println("Attempting to reconnect STA...");
    // WiFi.reconnect();
    // syncTimeWithNTP();
}

void startWifi() {
    if(isWiFiActive()) {
        Serial.println("Wifi is already connected.");
        return;
    }
    const SystemSettings* s = g_systemSettings;

    if(strlen(s->externalSSID) > 0) {
        Serial.println("Start APSta: " + String(s->externalSSID));
        WiFi.begin(s->externalSSID, s->externalPW);
        WiFi.softAP(s->internalSSID, s->internalPW, WIFI_CHANNEL, 0, MAX_CLIENTS);
        WiFi.mode(WIFI_MODE_APSTA);
    } else {
        Serial.println("Start AP");
        WiFi.mode(WIFI_MODE_AP);
    }
    server.begin();
    dnsServer.start(53, "*", localIP);
}

void stopWifi() {
    Serial.println("stopWifi: Stopping all WiFi services");
    if(!isWiFiActive()) {
        Serial.println("WiFi is not active. Nothing to stop.");
        return;
    }

    dnsServer.stop();
    server.end();
    // WiFi.disconnect(true);
    // WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_MODE_NULL);

    Serial.println("stopWifi: All WiFi services stopped");
}

bool isWiFiActive() {
    wifi_mode_t currentMode = WiFi.getMode();
    return currentMode == WIFI_MODE_AP || currentMode == WIFI_MODE_APSTA;
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
        g_wifiTracker->staConfigValid = true;
        g_wifiTracker->lastStaConnectionTime = millis();
        syncTimeWithNTP();
        updateTelemetryWiFiStatus();
        break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
        updateTelemetryWiFiStatus();
        Serial.println("STA Disconnected");

        if(WiFi.getMode() == WIFI_MODE_APSTA) {
            g_wifiTracker->staConfigValid = false;
            if(retryCount < 10) {
                // delay(200);
                Serial.println("Retrying STA connection, attempt " + String(retryCount + 1));
                retryCount++;
                return;
            } else {
                WiFi.mode(WIFI_MODE_AP);
                // g_wifiTracker->staConfigValid = false;
                Serial.println("Max retry attempts reached. Switching to AP mode only.");
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
                [r](AsyncWebServerRequest* request) {
                    // Body will be available in the body handler callback
                    r.handler(request, String(""));
                },
                NULL,
                [r](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    String body = String((char*)data);
                    body = body.substring(0, len);
                    // Call handler with actual body data
                    r.handler(request, body);
                });
        } else {
            // For GET routes, pass empty body
            server.on(r.uri, r.method, [r](AsyncWebServerRequest* request) { r.handler(request, String("")); });
        }
    }

    // the catch all
    server.onNotFound([localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
        Serial.print("onnotfound ");
        Serial.print(request->host()); // This gives some insight into whatever was being
                                       // requested on the serial monitor
        Serial.print(" ");
        Serial.print(request->url());
        Serial.print(" sent redirect to " + localIPURL + "\n");
    });
}