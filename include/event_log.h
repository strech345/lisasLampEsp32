#ifndef EVENT_LOG_H
#define EVENT_LOG_H
// Ereignisprotokoll: speichert bis zu 200 Einträge im RAM-Ringpuffer,
// thread-sicher per FreeRTOS-Mutex, abrufbar als JSON über die Web-API.

#include <Arduino.h>

// Schreibt eine Meldung mit Uptime-ms und Unix-Zeitstempel in den Ringpuffer
void logEvent(const char* message);

// Gibt alle Einträge als JSON zurück (neueste zuerst)
String getEventLogJson();

// Ereignisprotokoll an/aus (bestehende Einträge bleiben)
void setEventLogEnabled(bool enabled);

#endif // EVENT_LOG_H
