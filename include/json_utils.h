#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h"

// Passwörter werden in der API immer als Maske zurückgegeben — nie im Klartext
const char* const PASSWORD_MASK = "******";

// Serialisiert die Lampenkonfiguration (Farbe, Modi, Alarme) als JSON-String
String createConfigJson(const FullConfig& config);

// Deserialisiert JSON in FullConfig; gibt false bei Parse-Fehler zurück
bool parseConfigJson(const String& jsonString, FullConfig& config);

// Serialisiert die Systemeinstellungen (SSID, Passwörter maskiert, Flags) als JSON-String
String createSystemConfigJson(const SystemSettings& systemSettings);

// Deserialisiert JSON in SystemSettings; Passwörter werden nur übernommen wenn nicht Maske
bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings);

// Serialisiert den WiFi-Status (Sync-Zeit, Verbindungsinfo) als JSON-String
String createWiFiStatusJson(const WiFiStatus& status);

#endif // JSON_UTILS_H
