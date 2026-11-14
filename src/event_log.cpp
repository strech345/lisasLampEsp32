#include "event_log.h"
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

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
// Mutex weil logEvent() auch aus ISR-Kontext (WiFi-Events) aufgerufen werden kann
static portMUX_TYPE s_logMux = portMUX_INITIALIZER_UNLOCKED;

void logEvent(const char* message) {
    uint8_t idx;
    taskENTER_CRITICAL(&s_logMux);
    if(s_count == MAX_LOG_EVENTS) {
        // Puffer voll: ältesten Eintrag überschreiben
        idx = s_head;
        s_head = (s_head + 1) % MAX_LOG_EVENTS;
    } else {
        idx = (s_head + s_count) % MAX_LOG_EVENTS;
        s_count++;
    }
    s_log[idx].uptimeMs = millis();
    time_t t = time(nullptr);
    s_log[idx].unixTime = (t >= 1577836800L) ? (uint32_t)t : 0;
    strncpy(s_log[idx].msg, message, EVENT_MSG_LEN - 1);
    s_log[idx].msg[EVENT_MSG_LEN - 1] = '\0';
    taskEXIT_CRITICAL(&s_logMux);
}

String getEventLogJson() {
    taskENTER_CRITICAL(&s_logMux);
    unsigned long nowMs = millis();
    time_t nowUnix = time(nullptr);
    uint32_t nowUnixL = (nowUnix >= 1577836800L) ? (uint32_t)nowUnix : 0;

    String json = "{\"uptimeMs\":";
    json += String(nowMs);
    json += ",\"unixTime\":";
    json += String(nowUnixL);
    json += ",\"events\":[";

    bool first = true;
    // Neueste Einträge zuerst ausgeben (s_count-1 = neuester Eintrag)
    for(int i = (int)s_count - 1; i >= 0; i--) {
        uint8_t idx = (s_head + (uint8_t)i) % MAX_LOG_EVENTS;
        if(!first) json += ",";
        first = false;
        json += "{\"uptimeMs\":";
        json += String(s_log[idx].uptimeMs);
        json += ",\"unixTime\":";
        json += String(s_log[idx].unixTime);
        json += ",\"msg\":\"";
        json += String(s_log[idx].msg);
        json += "\"}";
    }
    json += "]}";
    taskEXIT_CRITICAL(&s_logMux);
    return json;
}
