// Ringpuffer im RAM, kein Flash-Verschleiß; Mutex weil WiFi-Events aus einem anderen Task kommen.
#include "event_log.h"
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <ArduinoJson.h>

#define MAX_LOG_EVENTS 200
#define EVENT_MSG_LEN 52

struct LogEvent {
    unsigned long uptimeMs;
    uint32_t unixTime; // 0 = Uhrzeit war zum Zeitpunkt des Eintrags unbekannt
    char msg[EVENT_MSG_LEN];
};

// Ringpuffer: s_head zeigt auf den ältesten Eintrag, s_count auf die Anzahl
static LogEvent s_log[MAX_LOG_EVENTS];
static uint8_t s_head = 0;
static uint8_t s_count = 0;

static StaticSemaphore_t s_logMutexBuf;
static SemaphoreHandle_t s_logMutex = xSemaphoreCreateMutexStatic(&s_logMutexBuf);

void logEvent(const char* message) {
    // Zeit zuerst holen, außerhalb jeder Sperre
    unsigned long nowMs = millis();
    time_t t = time(nullptr);
    uint32_t unixTime = (t >= 1577836800L) ? (uint32_t)t : 0;

    xSemaphoreTake(s_logMutex, portMAX_DELAY);
    uint8_t idx;
    if(s_count == MAX_LOG_EVENTS) {
        // Puffer voll: ältesten Eintrag überschreiben
        idx = s_head;
        s_head = (s_head + 1) % MAX_LOG_EVENTS;
    } else {
        idx = (s_head + s_count) % MAX_LOG_EVENTS;
        s_count++;
    }
    s_log[idx].uptimeMs = nowMs;
    s_log[idx].unixTime = unixTime;
    strncpy(s_log[idx].msg, message, EVENT_MSG_LEN - 1);
    s_log[idx].msg[EVENT_MSG_LEN - 1] = '\0';
    xSemaphoreGive(s_logMutex);
}

// Baut das Log-JSON: Einträge werden kurz unter dem Mutex in einen Heap-Puffer kopiert und
// danach (ohne Sperre) mit ArduinoJson einzeln serialisiert -> korrektes Escaping, wenig RAM-Spitze.
String getEventLogJson() {
    unsigned long nowMs = millis();
    time_t nowUnix = time(nullptr);
    uint32_t nowUnixL = (nowUnix >= 1577836800L) ? (uint32_t)nowUnix : 0;

    // Lokale Kopie (neueste zuerst), Mutex nur für das Kopieren halten
    LogEvent* copy = (LogEvent*)malloc(sizeof(LogEvent) * MAX_LOG_EVENTS);
    size_t n = 0;
    if(copy) {
        xSemaphoreTake(s_logMutex, portMAX_DELAY);
        n = s_count;
        for(size_t i = 0; i < n; i++) {
            // i = 0 -> neuester Eintrag (s_count-1 relativ zu s_head)
            uint8_t idx = (s_head + (uint8_t)(n - 1 - i)) % MAX_LOG_EVENTS;
            copy[i] = s_log[idx];
        }
        xSemaphoreGive(s_logMutex);
    }

    String json;
    json.reserve(80 + n * 90);
    json = "{\"uptimeMs\":";
    json += String(nowMs);
    json += ",\"unixTime\":";
    json += String(nowUnixL);
    json += ",\"events\":[";

    String entry; // wiederverwendeter Puffer für einen Eintrag
    for(size_t i = 0; i < n; i++) {
        if(i > 0)
            json += ",";
        JsonDocument doc; // pro Eintrag ein kleines Dokument → geringe RAM-Spitze
        doc["uptimeMs"] = copy[i].uptimeMs;
        doc["unixTime"] = copy[i].unixTime;
        doc["msg"] = (const char*)copy[i].msg; // wird von ArduinoJson beim Serialisieren escaped
        // Achtung: serializeJson() in einen String LEERT diesen vorher (ArduinoJson 7) — daher erst
        // in `entry` serialisieren und dann anhängen, nicht direkt in `json`
        serializeJson(doc, entry);
        json += entry;
    }
    free(copy);

    json += "]}";
    return json;
}
