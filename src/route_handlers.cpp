// HTTP-Routen-Handler — Implementierung
// Jeder Handler liest den Request (und ggf. Body), wandelt JSON um und
// ruft den Zustandsänderungs-Callback auf, damit main.cpp den neuen Zustand übernimmt.
#include "route_handlers.h"
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "debug_utils.h"
#include "json_utils.h"
#include "types.h"
#include "store.h"
#include "event_log.h"
#include "tsf_clock.h"
#include "rds_clock.h"
#include "rtc_crystal.h"
#include "wifi_controller.h"
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>

// Zeiger auf globalen Zustand — werden in initRouteHandlers gesetzt
static const FullConfig* g_config = nullptr;
static const SystemSettings* g_systemSettings = nullptr;
static const WiFiTestTracker* g_wifiTracker = nullptr;
static GenericStateUpdateCallback g_stateCallback = nullptr;

// Sendet eine gzip-komprimierte Datei aus LittleFS mit korrekten Cache-Headern
static void sendGzipFile(AsyncWebServerRequest* request, const char* path, const char* mimeType) {
    if(!LittleFS.exists(path)) {
        request->send(404, "text/plain", "Not Found");
        return;
    }
    AsyncWebServerResponse* response = request->beginResponse(LittleFS, path, mimeType);
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "public,max-age=31536000");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

void handlePing(AsyncWebServerRequest* request, const String&) {
    request->send(200, "text/plain", "pong");
}

void handleRoot(AsyncWebServerRequest* request, const String&) {
    serialPrint("handleRoot: " + request->url());

    const char* indexPath = "/index.html.gzip";
    if(!LittleFS.exists(indexPath)) {
        serialPrint("index.html.gz not found");
        request->send(404, "text/plain", "index.html.gz not found");
        return;
    }

    AsyncWebServerResponse* response = request->beginResponse(LittleFS, indexPath, "text/html");
    if(response) {
        response->addHeader("Content-Encoding", "gzip");
        response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
        response->addHeader("Set-Cookie", "portal_opened=1; Path=/; Max-Age=3600");
        request->send(response);
    } else {
        request->send(404, "text/plain", "File Not Found");
    }
}

void handlePicoCSS(AsyncWebServerRequest* request, const String&) {
    serialPrint("handlePicoCSS: " + request->url());
    sendGzipFile(request, "/pico.min.css.gzip", "text/css");
}

void handleAppJS(AsyncWebServerRequest* request, const String&) {
    serialPrint("handleAppJS: " + request->url());
    sendGzipFile(request, "/app.js.gzip", "application/javascript");
}

void handleAppCSS(AsyncWebServerRequest* request, const String&) {
    serialPrint("handleAppCSS: " + request->url());
    sendGzipFile(request, "/app.css.gzip", "text/css");
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

    FullConfig newConfig = *g_config;

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

    SystemSettings newSystemSettings = *g_systemSettings;

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
    serialPrint(String("send status clockSynced=") + (status.clockSynced ? "true" : "false"));

    request->send(200, "application/json", createWiFiStatusJson(status));
}

void handleMeerkatImage(AsyncWebServerRequest* request, const String&) {
    const char* imagePath = "/meerkat.avif";
    if(!LittleFS.exists(imagePath)) {
        serialPrint("meerkat.avif not found");
        request->send(404, "text/plain", "Image Not Found");
        return;
    }
    request->send(LittleFS, imagePath, "image/avif");
}

// Registriert den OTA-Endpunkt für Firmware-Updates per HTTP-Upload
void registerOtaRoute(AsyncWebServer& server) {
    server.on(
        "/ota", HTTP_POST,
        // Wird aufgerufen wenn der Upload vollständig ist — Antwort senden, dann Neustart
        [](AsyncWebServerRequest* request) {
            bool ok = !Update.hasError();
            request->send(200, "application/json",
                          ok ? "{\"status\":\"ok\"}" : "{\"status\":\"error\",\"message\":\"Update failed\"}");
            if(ok) {
                // Kurze Pause damit die HTTP-Antwort noch gesendet werden kann
                delay(300);
                ESP.restart();
            }
        },
        // Wird für jeden Datenblock der Firmware-Binary aufgerufen
        [](AsyncWebServerRequest* request, String filename, size_t index, uint8_t* data, size_t len, bool final) {
            if(index == 0) {
                serialPrint("OTA start: " + filename);
                // UPDATE_SIZE_UNKNOWN: gesamte OTA-Partition verwenden
                if(!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                    serialPrint("OTA begin failed: " + String(Update.errorString()));
                    return;
                }
            }
            if(Update.write(data, len) != len) {
                Update.abort();
                serialPrint("OTA write failed: " + String(Update.errorString()));
                return;
            }
            if(final) {
                if(!Update.end(true)) {
                    serialPrint("OTA end failed: " + String(Update.errorString()));
                } else {
                    serialPrint("OTA complete: " + String(index + len) + " bytes");
                }
            }
        });
}

void handleGetWarnings(AsyncWebServerRequest* request, const String&) {

    bool enabled = g_systemSettings ? g_systemSettings->warningsEnabled : true;
    String json = String("{\"enabled\":") + (enabled ? "true" : "false") + "}";
    request->send(200, "application/json", json);
}

void handleGetDeepSleep(AsyncWebServerRequest* request, const String&) {

    bool enabled = g_systemSettings ? g_systemSettings->deepSleepEnabled : true;
    String json = String("{\"enabled\":") + (enabled ? "true" : "false") + "}";
    request->send(200, "application/json", json);
}

void handleGetEventLog(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getEventLogJson());
}

void handleRestart(AsyncWebServerRequest* request, const String&) {

    logEvent("Restart via web");
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    delay(200);
    ESP.restart();
}

void handleFactoryReset(AsyncWebServerRequest* request, const String&) {

    logEvent("Factory reset via web");
    resetFullConfig();
    resetSystemSettings();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
    delay(200);
    ESP.restart();
}

void handleTriggerNtpSync(AsyncWebServerRequest* request, const String&) {

    logEvent("NTP sync triggered via web");
    syncTimeWithNTP();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleSetWarnings(AsyncWebServerRequest* request, const String& body) {

    if(g_stateCallback == nullptr) {
        request->send(500, "text/plain", "No callback registered");
        return;
    }
    if(body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request: Missing JSON body.");
        return;
    }
    JsonDocument doc;
    if(deserializeJson(doc, body) || !doc.containsKey("enabled")) {
        request->send(400, "text/plain", "Invalid JSON.");
        return;
    }
    bool enabled = doc["enabled"].as<bool>();
    g_stateCallback(STATE_CHANGE_WARNINGS, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

void handleSetDeepSleep(AsyncWebServerRequest* request, const String& body) {

    if(g_stateCallback == nullptr) {
        request->send(500, "text/plain", "No callback registered");
        return;
    }
    if(body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request: Missing JSON body.");
        return;
    }
    JsonDocument doc;
    if(deserializeJson(doc, body) || !doc.containsKey("enabled")) {
        request->send(400, "text/plain", "Invalid JSON.");
        return;
    }
    bool enabled = doc["enabled"].as<bool>();
    g_stateCallback(STATE_CHANGE_DEEP_SLEEP, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

// Initialisiert Handler-Zustand und gibt alle Routen zurück
std::vector<Route> initRouteHandlers(const FullConfig* config, const SystemSettings* systemSettings,
                                     const WiFiTestTracker* wifiTracker, GenericStateUpdateCallback stateCallback) {
    g_config = config;
    g_systemSettings = systemSettings;
    g_wifiTracker = wifiTracker;
    g_stateCallback = stateCallback;

    // --- Routentabelle: URL → Handler ---
    return {{"/", HTTP_GET, handleRoot},
            {"/pico.min.css", HTTP_GET, handlePicoCSS},
            {"/app.js", HTTP_GET, handleAppJS},
            {"/app.css", HTTP_GET, handleAppCSS},
            {"/get_config", HTTP_GET, handleGetConfig},
            {"/set_config", HTTP_POST, handleSetConfig},
            {"/get_system_config", HTTP_GET, handleGetSystemConfig},
            {"/set_system_config", HTTP_POST, handleSetSystemConfig},
            {"/get_status", HTTP_GET, handleGetStatus},
            {"/meerkat.avif", HTTP_GET, handleMeerkatImage},
            {"/get_warnings", HTTP_GET, handleGetWarnings},
            {"/set_warnings", HTTP_POST, handleSetWarnings},
            {"/get_deep_sleep", HTTP_GET, handleGetDeepSleep},
            {"/set_deep_sleep", HTTP_POST, handleSetDeepSleep},
            {"/get_event_log", HTTP_GET, handleGetEventLog},
            {"/restart", HTTP_POST, handleRestart},
            {"/ping", HTTP_GET, handlePing},
            {"/get_tsf_fallback", HTTP_GET, handleGetTsfFallback},
            {"/set_tsf_fallback", HTTP_POST, handleSetTsfFallback},
            {"/trigger_tsf_snapshot", HTTP_POST, handleTriggerTsfSnapshot},
            {"/get_rds_clock", HTTP_GET, handleGetRdsClock},
            {"/set_rds_clock", HTTP_POST, handleSetRdsClock},
            {"/get_rtc_crystal", HTTP_GET, handleGetRtcCrystal},
            {"/set_rtc_crystal", HTTP_POST, handleSetRtcCrystal},
            {"/trigger_ntp_sync", HTTP_POST, handleTriggerNtpSync},
            {"/factory_reset", HTTP_POST, handleFactoryReset}};
}

void handleGetTsfFallback(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getTsfStatusJson());
}

void handleSetTsfFallback(AsyncWebServerRequest* request, const String& body) {

    if(g_stateCallback == nullptr || body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request");
        return;
    }
    JsonDocument doc;
    if(deserializeJson(doc, body)) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }
    SystemSettings newSettings = *g_systemSettings;
    newSettings.tsfFallbackEnabled = doc["enabled"] | true;
    g_stateCallback(STATE_CHANGE_SYSTEM_CONFIG, static_cast<void*>(&newSettings));
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleTriggerTsfSnapshot(AsyncWebServerRequest* request, const String&) {

    triggerTsfSnapshot();
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetRdsClock(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRdsStatusJson());
}

void handleSetRdsClock(AsyncWebServerRequest* request, const String& body) {

    if(g_stateCallback == nullptr || body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request");
        return;
    }
    JsonDocument doc;
    if(deserializeJson(doc, body)) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }
    SystemSettings newSettings = *g_systemSettings;
    newSettings.rdsClockEnabled = doc["enabled"] | false;
    g_stateCallback(STATE_CHANGE_SYSTEM_CONFIG, static_cast<void*>(&newSettings));
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetRtcCrystal(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRtcCrystalStatusJson());
}

// Einstellung speichern und Neustart auslösen (RTC-Taktquelle nur beim Boot umschaltbar)
void handleSetRtcCrystal(AsyncWebServerRequest* request, const String& body) {

    if(g_stateCallback == nullptr || body.isEmpty()) {
        request->send(400, "text/plain", "Bad Request");
        return;
    }
    JsonDocument doc;
    if(deserializeJson(doc, body)) {
        request->send(400, "text/plain", "Invalid JSON");
        return;
    }
    SystemSettings newSettings = *g_systemSettings;
    newSettings.rtcCrystalEnabled = doc["enabled"] | false;
    g_stateCallback(STATE_CHANGE_SYSTEM_CONFIG, static_cast<void*>(&newSettings));
    logEvent("RTC: Einstellung geändert, Neustart");
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
    delay(200);
    ESP.restart();
}