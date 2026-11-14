#ifndef EVENT_LOG_H
#define EVENT_LOG_H

#include <Arduino.h>

// Logs an event with the current uptime and unix timestamp (0 if unknown).
void logEvent(const char* message);

// Returns a JSON string with all log entries, newest first.
String getEventLogJson();

#endif // EVENT_LOG_H
