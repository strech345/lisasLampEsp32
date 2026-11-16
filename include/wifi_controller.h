#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>
#include "types.h"
#include <DNSServer.h>

static const unsigned long STOP_WIFI_AFTER_MS = 5 * 60 * 1000;       // 5 Minuten ohne Client → WiFi abschalten
static const unsigned long START_WIFI_AFTER_MS = 5 * 60 * 60 * 1000; // alle 5h automatisch für NTP-Sync starten

void wifiLoop();                  // Muss in loop() aufgerufen werden
void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker);

// withAP=true  → APSTA: Hotspot sichtbar, Web-UI erreichbar (manuell oder bei Systemeinstellungsänderung)
// withAP=false → nur STA: stiller NTP-Sync ohne sichtbaren Hotspot
void startWifi(bool withAP = true);
void stopWifi();

void handleClientRequests();  // DNS-Captive-Portal-Anfragen verarbeiten
void checkWifiStart();        // Automatischer Start nach START_WIFI_AFTER_MS
void checkWifiStop();         // Automatischer Stop nach STOP_WIFI_AFTER_MS ohne Client
bool isWiFiActive();
bool hasWifiClients();        // Gibt true zurück wenn mindestens ein Gerät verbunden ist

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info); // Intern: reagiert auf STA-Verbindungsereignisse
void tryReconnectSta();       // Alle 5min: versucht STA-Verbindung wiederherzustellen (Primär/Fallback-Wechsel)
void syncTimeWithNTP();       // SNTP-Sync neu starten; Drift wird im Callback berechnet
bool isTimeSyncedWithNTP();
void updateTelemetryWiFiStatus(); // Sync-Status minütlich prüfen und NVS-Zeitstempel sichern

#endif // WIFI_CONTROLLER_H
