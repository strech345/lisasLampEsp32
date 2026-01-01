
#include "route_handlers.h"
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "debug_utils.h"
#include "json_utils.h"
#include "types.h"
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

// Global state pointers for handlers
static const FullConfig* g_config = nullptr;
static const SystemSettings* g_systemSettings = nullptr;
static const WiFiTestTracker* g_wifiTracker = nullptr;
static GenericStateUpdateCallback g_stateCallback = nullptr;

// Root handler moved here from main.cpp to group route implementations.
void handleRoot(AsyncWebServerRequest* request, const String&) {
    Serial.print("handleRoot: ");
    Serial.println(request->url());

    const char* indexPath = "/index.html.gzip";
    if(!LittleFS.exists(indexPath)) {
        Serial.println("index.html.gz not found");
        request->send(404, "text/plain", "index.html.gz not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, indexPath, "text/html");
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public,max-age=31536000");
        response->addHeader("Set-Cookie", "portal_opened=1; Path=/; Max-Age=3600");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

// Serve compressed CSS files
void handlePicoCSS(AsyncWebServerRequest* request, const String&) {
    const char* cssPath = "/pico.min.css.gzip";

    if(!LittleFS.exists(cssPath)) {
        Serial.println("pico.min.css.gz not found");
        request->send(404, "text/plain", "pico.min.css.gz not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, cssPath, "text/css");
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public,max-age=31536000");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

// Serve compressed JavaScript files
void handleAppJS(AsyncWebServerRequest* request, const String&) {
    const char* jsPath = "/app.js.gzip";

    if(!LittleFS.exists(jsPath)) {
        Serial.println("app.js.gz not found");
        request->send(404, "text/plain", "app.js.gz not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, jsPath, "application/javascript");
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public,max-age=31536000");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

// Serve compressed CSS files for Svelte app
void handleAppCSS(AsyncWebServerRequest* request, const String&) {
    const char* cssPath = "/app.css.gzip";

    if(!LittleFS.exists(cssPath)) {
        Serial.println("app.css.gz not found");
        request->send(404, "text/plain", "app.css.gz not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, cssPath, "text/css");
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public,max-age=31536000");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

void handleGetConfig(AsyncWebServerRequest* request, const String&) {
    serialPrint(String("handleGetConfig: ") + request->url());
    if(g_config == nullptr) {
        request->send(500, "text/plain", "Configuration not initialized");
        return;
    }
    String jsonResponse = createConfigJson(*g_config);
    request->send(200, "application/json", jsonResponse);
    serialPrint("Sent /get_config response: " + jsonResponse);
}

void handleSetConfig(AsyncWebServerRequest* request, const String& body) {
    serialPrint(String("handleSetConfig: ") + request->url());
    if(g_stateCallback == nullptr) {
        request->send(500, "text/plain", "No callback registered for state changes");
        return;
    }

    if(body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request: Missing JSON body.");
        return;
    }
    serialPrint("Received /set_config body: " + body);

    FullConfig newConfig;

    if(!parseConfigJson(body, newConfig)) {
        request->send(400, "text/plain", "Invalid JSON or parsing failed.");
        serialPrint("Failed to parse /set_config JSON.");
        return;
    }

    g_stateCallback(STATE_CHANGE_CONFIG, static_cast<void*>(&newConfig));
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated\"}");
    serialPrint("Configuration updated via WiFi. Notifying main application.");
}

void handleGetSystemConfig(AsyncWebServerRequest* request, const String&) {
    serialPrint(String("handleGetSystemConfig: ") + request->url());
    if(g_systemSettings == nullptr) {
        request->send(500, "text/plain", "System settings not initialized");
        return;
    }
    String jsonResponse = createSystemConfigJson(*g_systemSettings);
    request->send(200, "application/json", jsonResponse);
    serialPrint("Sent /get_system_config response: " + jsonResponse);
}

void handleSetSystemConfig(AsyncWebServerRequest* request, const String& body) {
    serialPrint(String("handleSetSystemConfig: ") + request->url());
    if(g_stateCallback == nullptr) {
        request->send(500, "text/plain", "No callback registered for state changes");
        return;
    }
    if(body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request: Missing JSON body.");
        return;
    }
    serialPrint("Received /set_system_config body: " + body);

    SystemSettings newSystemSettings;

    if(!parseSystemConfigJson(body, newSystemSettings)) {
        request->send(400, "text/plain", "Invalid JSON or parsing failed.");
        serialPrint("Failed to parse /set_system_config JSON.");
        return;
    }

    g_stateCallback(STATE_CHANGE_SYSTEM_CONFIG, static_cast<void*>(&newSystemSettings));
    request->send(200, "application/json",
                  "{\"status\":\"success\",\"message\":\"System configuration "
                  "updated\"}");
    serialPrint("System configuration updated via WiFi. Notifying main application.");
}

void handleGetStatus(AsyncWebServerRequest* request, const String&) {
    if(g_wifiTracker == nullptr) {
        request->send(500, "text/plain", "WiFi tracker not initialized");
        return;
    }

    WiFiStatus status;
    status.lastTestResult = g_wifiTracker->lastDateResult;
    status.timeSinceLastTestMs = millis() - g_wifiTracker->lastTestTime;
    status.timeSinceLastSucceededTestMs = millis() - g_wifiTracker->lastSucceededTestTime;
    status.clockSynced = g_wifiTracker->clockSynced;
    status.lastStaConnectionTime = g_wifiTracker->lastStaConnectionTime;
    status.staConfigValid = g_wifiTracker->staConfigValid;
    status.systemTime = time(NULL);

    if(g_wifiTracker->clockSynced) {
        struct tm timeinfo;
        if(getLocalTime(&timeinfo)) {
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
            status.currentTime = String(buf);
        }
    }
    serialPrint("send status " + status.clockSynced);

    request->send(200, "application/json", createWiFiStatusJson(status));
}

// Initialization functions to set global state and return routes
std::vector<Route> initRouteHandlers(const FullConfig* config, const SystemSettings* systemSettings,
                                     const WiFiTestTracker* wifiTracker, GenericStateUpdateCallback stateCallback) {
    g_config = config;
    g_systemSettings = systemSettings;
    g_wifiTracker = wifiTracker;
    g_stateCallback = stateCallback;

    return {{"/", HTTP_ANY, handleRoot},
            {"/pico.min.css", HTTP_GET, handlePicoCSS},
            {"/app.js", HTTP_GET, handleAppJS},
            {"/app.css", HTTP_GET, handleAppCSS},
            {"/get_config", HTTP_GET, handleGetConfig},
            {"/set_config", HTTP_POST, handleSetConfig},
            {"/get_system_config", HTTP_GET, handleGetSystemConfig},
            {"/set_system_config", HTTP_POST, handleSetSystemConfig},
            {"/get_status", HTTP_GET, handleGetStatus}};
}