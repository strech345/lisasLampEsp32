// HTTP-Routen-Handler — Implementierung
// Jeder Handler liest den Request (und ggf. Body), validiert das JSON und legt über den
// Zustandsänderungs-Callback nur einen AUFTRAG für loop() ab (K7). Handler laufen im async_tcp-Task
// und führen selbst keine Seiteneffekte aus (keine LEDs, kein WLAN-Neustart, kein Flash, kein Restart).
// POST-Header-Prüfung (Content-Type/Origin) und Body-Limit liegen in captive_portal.cpp.
#include "route_handlers.h"
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "debug_utils.h"
#include "json_utils.h"
#include "types.h"
#include "store.h"
#include "event_log.h"
#include "tsf_clock.h" // getTsfStatusJson
#include "rds_clock.h"
#include "rtc_crystal.h"
#include "wifi_controller.h"
#include <vector>
#include <Arduino.h>
#include <ArduinoJson.h>

// Zeiger auf globalen Zustand — werden in initRouteHandlers gesetzt
static const FullConfig* g_config = nullptr;
static const SystemSettings* g_systemSettings = nullptr;
static const WiFiTestTracker* g_wifiTracker = nullptr;
static GenericStateUpdateCallback g_stateCallback = nullptr;

// Sendet eine Fehlerantwort als JSON mit deutscher Meldung (Meldungen enthalten keine Anführungszeichen)
static void sendError(AsyncWebServerRequest* request, int code, const char* message) {
    request->send(code, "application/json", String("{\"status\":\"error\",\"message\":\"") + message + "\"}");
}

// Parst {"enabled": bool}; sendet bei Fehler 400 und gibt false zurück
static bool parseEnabledBody(AsyncWebServerRequest* request, const String& body, bool& enabled) {
    if(g_stateCallback == nullptr) {
        sendError(request, 500, "Kein Callback registriert.");
        return false;
    }
    JsonDocument doc;
    if(body.isEmpty() || deserializeJson(doc, body) || !doc["enabled"].is<bool>()) {
        sendError(request, 400, "Ungültiges JSON: Feld enabled (true/false) fehlt.");
        return false;
    }
    enabled = doc["enabled"].as<bool>();
    return true;
}

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

void handleGetConfig(AsyncWebServerRequest* request, const String&) {

    serialPrint(String("handleGetConfig: ") + request->url());
    if(g_config == nullptr) {
        request->send(500, "text/plain", "Configuration not initialized");
        return;
    }
    String jsonResponse = createConfigJson(*g_config);
    request->send(200, "application/json", jsonResponse);
}

// Validiert die neue Lampenkonfiguration und legt sie als Auftrag ab (Helligkeit bleibt unverändert)
void handleSetConfig(AsyncWebServerRequest* request, const String& body) {

    serialPrint(String("handleSetConfig: ") + request->url());
    if(g_stateCallback == nullptr) {
        sendError(request, 500, "Kein Callback registriert.");
        return;
    }

    if(body.isEmpty()) {
        sendError(request, 400, "JSON-Body fehlt.");
        return;
    }

    FullConfig newConfig = *g_config;
    String error;
    if(!parseConfigJson(body, newConfig, &error)) {
        sendError(request, 400, error.c_str());
        serialPrint("Failed to parse /set_config JSON: " + error);
        return;
    }

    g_stateCallback(STATE_CHANGE_CONFIG, static_cast<void*>(&newConfig)); // Callback kopiert die Daten
    request->send(200, "application/json", "{\"status\":\"success\",\"message\":\"Configuration updated\"}");
}

void handleGetSystemConfig(AsyncWebServerRequest* request, const String&) {

    serialPrint(String("handleGetSystemConfig: ") + request->url());
    if(g_systemSettings == nullptr) {
        request->send(500, "text/plain", "System settings not initialized");
        return;
    }
    String jsonResponse = createSystemConfigJson(*g_systemSettings);
    request->send(200, "application/json", jsonResponse);
}

// Validiert die Netzwerkeinstellungen (SSID/Passwort-Längen) und legt sie als Auftrag ab.
// Der Body wird bewusst nicht geloggt (enthält Passwörter).
void handleSetSystemConfig(AsyncWebServerRequest* request, const String& body) {

    serialPrint(String("handleSetSystemConfig: ") + request->url());
    if(g_stateCallback == nullptr) {
        sendError(request, 500, "Kein Callback registriert.");
        return;
    }
    if(body.isEmpty()) {
        sendError(request, 400, "JSON-Body fehlt.");
        return;
    }

    SystemSettings newSystemSettings = *g_systemSettings;
    String error;
    if(!parseSystemConfigJson(body, newSystemSettings, &error)) {
        sendError(request, 400, error.c_str());
        serialPrint("Failed to parse /set_system_config JSON: " + error);
        return;
    }

    g_stateCallback(STATE_CHANGE_SYSTEM_CONFIG, static_cast<void*>(&newSystemSettings)); // Callback kopiert
    request->send(200, "application/json",
                  "{\"status\":\"success\",\"message\":\"System configuration updated\"}");
}

void handleGetStatus(AsyncWebServerRequest* request, const String&) {

    if(g_wifiTracker == nullptr) {
        request->send(500, "text/plain", "WiFi tracker not initialized");
        return;
    }

    WiFiStatus status;
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

// Neustart: Auftrag ablegen, loop() startet nach kurzer Verzögerung neu
void handleRestart(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_RESTART, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Werksreset: Auftrag ablegen, loop() setzt nach kurzer Verzögerung zurück und startet neu
void handleFactoryReset(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_FACTORY_RESET, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// NTP-Resync als Auftrag (wird in loop() ausgeführt)
void handleTriggerNtpSync(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_NTP_RESYNC, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Setzt das Warnungs-Flag (Auftrag)
void handleSetWarnings(AsyncWebServerRequest* request, const String& body) {

    bool enabled;
    if(!parseEnabledBody(request, body, enabled))
        return;
    g_stateCallback(STATE_CHANGE_WARNINGS, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"success\"}");
}

// Setzt das Deep-Sleep-Flag (Auftrag)
void handleSetDeepSleep(AsyncWebServerRequest* request, const String& body) {

    bool enabled;
    if(!parseEnabledBody(request, body, enabled))
        return;
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

// Setzt den TSF-Fallback (Auftrag, kein WLAN-Neustart)
void handleSetTsfFallback(AsyncWebServerRequest* request, const String& body) {

    bool enabled;
    if(!parseEnabledBody(request, body, enabled))
        return;
    g_stateCallback(STATE_CHANGE_TSF_FALLBACK, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// TSF-Snapshot als Auftrag (wird in loop() ausgeführt)
void handleTriggerTsfSnapshot(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_TSF_SNAPSHOT, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetRdsClock(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRdsStatusJson());
}

// Setzt die RDS-Uhrzeit (Auftrag, kein WLAN-Neustart)
void handleSetRdsClock(AsyncWebServerRequest* request, const String& body) {

    bool enabled;
    if(!parseEnabledBody(request, body, enabled))
        return;
    g_stateCallback(STATE_CHANGE_RDS_CLOCK, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

void handleGetRtcCrystal(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRtcCrystalStatusJson());
}

// Setzt den RTC-Quarz (Auftrag); loop() speichert und startet danach verzögert neu
// (RTC-Taktquelle nur beim Boot umschaltbar)
void handleSetRtcCrystal(AsyncWebServerRequest* request, const String& body) {

    bool enabled;
    if(!parseEnabledBody(request, body, enabled))
        return;
    g_stateCallback(STATE_CHANGE_RTC_CRYSTAL, static_cast<void*>(&enabled));
    request->send(200, "application/json", "{\"status\":\"restarting\"}");
}
