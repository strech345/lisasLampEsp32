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
#include "system_info.h" // getSystemInfoJson
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

// Schalter, die über POST /set_option gesetzt werden: JSON-Name → Auftragstyp
struct OptionDef {
    const char* name;
    StateChangeType type;
};
static const OptionDef OPTIONS[] = {
    {"warnings", STATE_CHANGE_WARNINGS},
    {"deepSleep", STATE_CHANGE_DEEP_SLEEP},
    {"tsfFallback", STATE_CHANGE_TSF_FALLBACK},
    {"rdsClock", STATE_CHANGE_RDS_CLOCK},
    {"rtcCrystal", STATE_CHANGE_RTC_CRYSTAL}, // löst danach einen Neustart aus
    {"statusLed", STATE_CHANGE_STATUS_LED},   // Debug
    {"serialLog", STATE_CHANGE_SERIAL_LOG},   // Debug
    {"eventLog", STATE_CHANGE_EVENT_LOG},     // Debug
    {"logUsb", STATE_CHANGE_LOG_USB},         // Debug: Logs über USB statt UART
};

// POST /set_option {"option": "warnings|deepSleep|tsfFallback|rdsClock|rtcCrystal|statusLed|serialLog|
//                    eventLog|logUsb", "enabled": bool}
// Legt einen Auftrag ab (Ausführung in loop(), kein WLAN-Neustart). Antwort bei rtcCrystal:
// {"status":"restarting"}, sonst {"status":"ok"}. Aktuelle Werte liefert /get_system_config.
static void handleSetOption(AsyncWebServerRequest* request, const String& body) {
    if(g_stateCallback == nullptr) {
        sendError(request, 500, "Kein Callback registriert.");
        return;
    }
    JsonDocument doc;
    if(body.isEmpty() || deserializeJson(doc, body) || !doc["option"].is<const char*>()
       || !doc["enabled"].is<bool>()) {
        sendError(request, 400, "Ungültiges JSON: option (Text) und enabled (true/false) erforderlich.");
        return;
    }
    const char* name = doc["option"];
    bool enabled = doc["enabled"];
    for(const OptionDef& opt : OPTIONS) {
        if(strcmp(opt.name, name) == 0) {
            g_stateCallback(opt.type, static_cast<void*>(&enabled)); // Callback kopiert den Wert
            bool restart = opt.type == STATE_CHANGE_RTC_CRYSTAL;
            request->send(200, "application/json", restart ? "{\"status\":\"restarting\"}" : "{\"status\":\"ok\"}");
            return;
        }
    }
    sendError(request, 400, "Unbekannte Option.");
}

// POST /set_led_count {"count": 1–MAX_LED_COUNT}: Anzahl der LEDs (sofort wirksam, gespeichert)
static void handleSetLedCount(AsyncWebServerRequest* request, const String& body) {
    if(g_stateCallback == nullptr) {
        sendError(request, 500, "Kein Callback registriert.");
        return;
    }
    JsonDocument doc;
    if(body.isEmpty() || deserializeJson(doc, body) || !doc["count"].is<int>()) {
        sendError(request, 400, "Ungültiges JSON: count (Zahl) erforderlich.");
        return;
    }
    int count = doc["count"];
    if(count < 1 || count > MAX_LED_COUNT) {
        sendError(request, 400, "LED-Anzahl muss 1–16 sein.");
        return;
    }
    uint8_t value = (uint8_t)count;
    g_stateCallback(STATE_CHANGE_LED_COUNT, static_cast<void*>(&value)); // Callback kopiert den Wert
    request->send(200, "application/json", "{\"status\":\"ok\"}");
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

static void handlePing(AsyncWebServerRequest* request, const String&) {
    request->send(200, "text/plain", "pong");
}

static void handleRoot(AsyncWebServerRequest* request, const String&) {
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

static void handlePicoCSS(AsyncWebServerRequest* request, const String&) {
    serialPrint("handlePicoCSS: " + request->url());
    sendGzipFile(request, "/pico.min.css.gzip", "text/css");
}

static void handleAppJS(AsyncWebServerRequest* request, const String&) {
    serialPrint("handleAppJS: " + request->url());
    sendGzipFile(request, "/app.js.gzip", "application/javascript");
}

static void handleGetConfig(AsyncWebServerRequest* request, const String&) {

    serialPrint(String("handleGetConfig: ") + request->url());
    if(g_config == nullptr) {
        request->send(500, "text/plain", "Configuration not initialized");
        return;
    }
    String jsonResponse = createConfigJson(*g_config);
    request->send(200, "application/json", jsonResponse);
}

// Validiert die neue Lampenkonfiguration und legt sie als Auftrag ab (Helligkeit bleibt unverändert)
static void handleSetConfig(AsyncWebServerRequest* request, const String& body) {

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

static void handleGetSystemConfig(AsyncWebServerRequest* request, const String&) {

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
static void handleSetSystemConfig(AsyncWebServerRequest* request, const String& body) {

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

// GET /get_status: Uhr- und Abgleich-Status (NTP, Heimnetz gerade an, letzter Versuch, nächster Abgleich)
static void handleGetStatus(AsyncWebServerRequest* request, const String&) {

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
    status.staActive = isStaActive();
    status.lastSyncOk = lastSyncOk();
    status.everSynced = g_wifiTracker->lastSucceededTestTime != 0;
    status.nextSyncInMs = msUntilNextSync();
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

static void handleMeerkatImage(AsyncWebServerRequest* request, const String&) {
    const char* imagePath = "/meerkat.avif";
    if(!LittleFS.exists(imagePath)) {
        serialPrint("meerkat.avif not found");
        request->send(404, "text/plain", "Image Not Found");
        return;
    }
    request->send(LittleFS, imagePath, "image/avif");
}

static void handleGetEventLog(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getEventLogJson());
}

// Neustart: Auftrag ablegen, loop() startet nach kurzer Verzögerung neu
static void handleRestart(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_RESTART, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// Werksreset: Auftrag ablegen, loop() setzt nach kurzer Verzögerung zurück und startet neu
static void handleFactoryReset(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_FACTORY_RESET, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

// NTP-Resync als Auftrag (wird in loop() ausgeführt)
static void handleTriggerNtpSync(AsyncWebServerRequest* request, const String&) {

    g_stateCallback(STATE_CHANGE_NTP_RESYNC, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleGetTsfFallback(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getTsfStatusJson());
}

// TSF-Snapshot als Auftrag (wird in loop() ausgeführt)
// POST /trigger_tsf_snapshot: nur bis 10 min nach einem NTP-Abgleich (sonst 409 mit Meldung)
static void handleTriggerTsfSnapshot(AsyncWebServerRequest* request, const String&) {
    if(!isTsfSnapshotAllowed()) {
        request->send(409, "application/json",
                      "{\"status\":\"error\",\"message\":\"Snapshot nur bis 10 min nach einem NTP-Abgleich (ohne Deep Sleep dazwischen) möglich – erst „Uhr jetzt synchronisieren“.\"}");
        return;
    }
    g_stateCallback(STATE_CHANGE_TSF_SNAPSHOT, nullptr);
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleGetRdsClock(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRdsStatusJson());
}

static void handleGetRtcCrystal(AsyncWebServerRequest* request, const String&) {

    request->send(200, "application/json", getRtcCrystalStatusJson());
}

// POST /trigger_wifi_scan: WLAN-Suche starten (Ergebnis über GET /get_wifi_scan abholen)
static void handleTriggerWifiScan(AsyncWebServerRequest* request, const String&) {
    if(!requestWifiScan()) {
        sendError(request, 409, "WLAN ist gerade beschäftigt.");
        return;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleGetWifiScan(AsyncWebServerRequest* request, const String&) {
    request->send(200, "application/json", getWifiScanJson());
}

// POST /test_wifi {"slot": 1|2, "ssid": "...", "password": "..."}: Verbindungstest starten.
// password "******" = gespeichertes Passwort des Feldes. Ergebnis über GET /get_wifi_test.
static void handleTestWifi(AsyncWebServerRequest* request, const String& body) {
    JsonDocument doc;
    if(body.isEmpty() || deserializeJson(doc, body) || !doc["ssid"].is<const char*>()) {
        sendError(request, 400, "Ungültiges JSON: ssid erforderlich.");
        return;
    }
    const char* ssid = doc["ssid"];
    const char* pw = doc["password"] | "";
    uint8_t slot = doc["slot"] | 1;
    size_t ssidLen = strlen(ssid), pwLen = strlen(pw);
    if(ssidLen < 1 || ssidLen > 32) {
        sendError(request, 400, "WLAN-Name muss 1–32 Zeichen lang sein.");
        return;
    }
    if(pwLen > 63 || (pwLen > 0 && pwLen < 8 && strcmp(pw, PASSWORD_MASK) != 0)) {
        sendError(request, 400, "Passwort muss leer oder 8–63 Zeichen lang sein.");
        return;
    }
    if(!requestWifiTest(slot == 2 ? 2 : 1, ssid, pw)) {
        sendError(request, 409, "WLAN ist gerade beschäftigt.");
        return;
    }
    request->send(200, "application/json", "{\"status\":\"ok\"}");
}

static void handleGetWifiTest(AsyncWebServerRequest* request, const String&) {
    request->send(200, "application/json", getWifiTestJson());
}

// Live-Systemdaten (Heap, CPU, Flash, Temperatur, Hotspot-Geräte …); die Absender-IP markiert das eigene Gerät
static void handleGetSystemInfo(AsyncWebServerRequest* request, const String&) {
    request->send(200, "application/json", getSystemInfoJson(request->client()->remoteIP()));
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
            {"/get_system_info", HTTP_GET, handleGetSystemInfo},
            {"/meerkat.avif", HTTP_GET, handleMeerkatImage},
            {"/set_option", HTTP_POST, handleSetOption},
            {"/set_led_count", HTTP_POST, handleSetLedCount},
            {"/get_event_log", HTTP_GET, handleGetEventLog},
            {"/restart", HTTP_POST, handleRestart},
            {"/ping", HTTP_GET, handlePing},
            {"/get_tsf_fallback", HTTP_GET, handleGetTsfFallback},
            {"/trigger_tsf_snapshot", HTTP_POST, handleTriggerTsfSnapshot},
            {"/get_rds_clock", HTTP_GET, handleGetRdsClock},
            {"/get_rtc_crystal", HTTP_GET, handleGetRtcCrystal},
            {"/trigger_ntp_sync", HTTP_POST, handleTriggerNtpSync},
            {"/trigger_wifi_scan", HTTP_POST, handleTriggerWifiScan},
            {"/get_wifi_scan", HTTP_GET, handleGetWifiScan},
            {"/test_wifi", HTTP_POST, handleTestWifi},
            {"/get_wifi_test", HTTP_GET, handleGetWifiTest},
            {"/factory_reset", HTTP_POST, handleFactoryReset}};
}
