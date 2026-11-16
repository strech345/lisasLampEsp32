#ifndef WEB_AUTH_H
#define WEB_AUTH_H
// Web-Authentifizierung: optionaler Passwortschutz mit Cookie-basierten Tokens.
// Kein Passwort gesetzt → alles offen. Passwort gesetzt → Token (Cookie "auth")
// ermöglicht dauerhaften Zugang ohne erneute Passwort-Eingabe (bis zu 8 Geräte).

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "types.h"

// Einmalige Initialisierung: Tokens aus NVS laden und Einstellungszeiger merken
void initWebAuth(const SystemSettings* settings);

// Prüft ob der Request autorisiert ist:
// - Kein Passwort gesetzt → true
// - Cookie "auth" mit bekanntem Token → true
// - sonst → false
bool isAuthorized(AsyncWebServerRequest* request);

// Erzeugt neuen Token, speichert ihn im FIFO (ältester fliegt raus wenn voll)
String createAndStoreToken();

// Anzahl aktuell gespeicherter Tokens (0–8)
uint8_t getTokenCount();

#endif // WEB_AUTH_H
