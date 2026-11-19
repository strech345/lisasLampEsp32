#ifndef JSON_UTILS_H
#define JSON_UTILS_H
// JSON-Serialisierung/-Deserialisierung für FullConfig, SystemSettings und WiFiStatus.
// Passwörter werden bei der Ausgabe immer maskiert; beim Einlesen wird die Maske ignoriert.

#include <Arduino.h>
#include <ArduinoJson.h>
#include "types.h"

// Passwörter werden in der API immer als Maske zurückgegeben — nie im Klartext
const char* const PASSWORD_MASK = "******";

// Serialisiert die Lampenkonfiguration (Farbe, Modi, Alarme) als JSON-String
String createConfigJson(const FullConfig& config);

// Deserialisiert JSON in FullConfig (Kopie der aktuellen Config; fehlende Felder bleiben erhalten).
// brightnessMode wird ignoriert. Wertebereiche werden geprüft; bei Fehler false + deutsche Meldung in *error.
bool parseConfigJson(const String& jsonString, FullConfig& config, String* error = nullptr);

// Serialisiert die Systemeinstellungen (SSID, Passwörter maskiert, Flags) als JSON-String
String createSystemConfigJson(const SystemSettings& systemSettings);

// Deserialisiert JSON in SystemSettings; Passwörter werden nur übernommen wenn nicht Maske.
// Prüft SSID-/Passwortlängen; Passwörter entfernen über clearInternalPW/clearExternalPW/clearExternalPW2 (bool).
// Bei Fehler false + deutsche Meldung in *error.
bool parseSystemConfigJson(const String& jsonString, SystemSettings& systemSettings, String* error = nullptr);

// Serialisiert den WiFi-Status (Sync-Zeit, Verbindungsinfo) als JSON-String
String createWiFiStatusJson(const WiFiStatus& status);

#endif // JSON_UTILS_H
