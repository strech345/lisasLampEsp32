#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>
#include "types.h"
#include <DNSServer.h>

static const unsigned long STOP_WIFI_AFTER_MS = 60 * 1000;                  // 30 seconds
static const unsigned long START_WIFI_AFTER_MS = 5 * 60 * 60 * 1000;        // 5 hours

void wifiLoop();
void startWifi();
void stopWifi();
void handleClientRequests();
void checkWifiStart();
void checkWifiStop();
bool isWiFiActive();
bool isWifiActiveAndNotUsed();
void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP);
void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes);
void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info);
void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker);
bool isTimeSyncedWithNTP();
void updateTelemetryWiFiStatus();
void tryReconnectSta();
void syncTimeWithNTP();
bool hasActiveClients();
bool hasWifiClients();

#endif // WIFI_CONTROLLER_H
