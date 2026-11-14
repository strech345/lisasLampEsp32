#include "event_log.h"
#include <time.h>

#define MAX_LOG_EVENTS 200
#define EVENT_MSG_LEN 52

struct LogEvent {
    unsigned long uptimeMs;
    long unixTime; // 0 = time was unknown at time of event
    char msg[EVENT_MSG_LEN];
};

static LogEvent s_log[MAX_LOG_EVENTS];
static uint8_t s_head = 0;  // index of oldest entry
static uint8_t s_count = 0; // number of stored entries

void logEvent(const char* message) {
    uint8_t idx;
    if(s_count == MAX_LOG_EVENTS) {
        // Overwrite oldest entry
        idx = s_head;
        s_head = (s_head + 1) % MAX_LOG_EVENTS;
    } else {
        idx = (s_head + s_count) % MAX_LOG_EVENTS;
        s_count++;
    }
    s_log[idx].uptimeMs = millis();
    time_t t = time(nullptr);
    s_log[idx].unixTime = (t >= 1577836800L) ? (long)t : 0;
    strncpy(s_log[idx].msg, message, EVENT_MSG_LEN - 1);
    s_log[idx].msg[EVENT_MSG_LEN - 1] = '\0';
}

String getEventLogJson() {
    unsigned long nowMs = millis();
    time_t nowUnix = time(nullptr);
    long nowUnixL = (nowUnix >= 1577836800L) ? (long)nowUnix : 0;

    String json = "{\"uptimeMs\":";
    json += String(nowMs);
    json += ",\"unixTime\":";
    json += String(nowUnixL);
    json += ",\"events\":[";

    bool first = true;
    // Newest first: iterate from s_count-1 down to 0
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
    return json;
}
