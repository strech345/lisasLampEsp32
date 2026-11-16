#ifndef ROUTE_HANDLERS_H
#define ROUTE_HANDLERS_H
// HTTP-Routen-Handler: registriert alle API-Endpunkte und den OTA-Endpunkt,
// nimmt Anfragen entgegen und leitet Zustandsänderungen über Callback an main.cpp weiter.

#include <ESPAsyncWebServer.h>
#include <functional>
#include "wifi_controller.h"
#include "types.h"

// Handler für alle HTTP-Endpunkte (Signatur passend zu Route)
void handlePing(AsyncWebServerRequest* request, const String& body);
void handleRoot(AsyncWebServerRequest* request, const String& body);
void handlePicoCSS(AsyncWebServerRequest* request, const String& body);
void handleAppJS(AsyncWebServerRequest* request, const String& body);
void handleAppCSS(AsyncWebServerRequest* request, const String& body);
void handleGetConfig(AsyncWebServerRequest* request, const String& body);
void handleSetConfig(AsyncWebServerRequest* request, const String& body);
void handleGetSystemConfig(AsyncWebServerRequest* request, const String& body);
void handleSetSystemConfig(AsyncWebServerRequest* request, const String& body);
void handleGetStatus(AsyncWebServerRequest* request, const String& body);
void handleMeerkatImage(AsyncWebServerRequest* request, const String& body);
void handleGetWarnings(AsyncWebServerRequest* request, const String& body);
void handleSetWarnings(AsyncWebServerRequest* request, const String& body);
void handleGetEventLog(AsyncWebServerRequest* request, const String& body);
void handleRestart(AsyncWebServerRequest* request, const String& body);
void handleGetTsfFallback(AsyncWebServerRequest* request, const String& body);
void handleSetTsfFallback(AsyncWebServerRequest* request, const String& body);
void handleTriggerTsfSnapshot(AsyncWebServerRequest* request, const String& body);
void handleGetRdsClock(AsyncWebServerRequest* request, const String& body);
void handleSetRdsClock(AsyncWebServerRequest* request, const String& body);
void handleGetRtcCrystal(AsyncWebServerRequest* request, const String& body);
void handleSetRtcCrystal(AsyncWebServerRequest* request, const String& body);

// Initialisiert Handler-Zustand und gibt alle Routen zurück
std::vector<Route> initRouteHandlers(const FullConfig* config, const SystemSettings* systemSettings,
                                     const WiFiTestTracker* wifiTracker, GenericStateUpdateCallback stateCallback);

// Registriert den Callback der bei Zustandsänderungen über die Web-API aufgerufen wird
void setOnStateChangedCallback(GenericStateUpdateCallback cb);

// Registriert den OTA-Endpunkt — muss vor server.begin() aufgerufen werden
// damit /ota nicht vom Captive-Portal-Catch-all abgefangen wird
void registerOtaRoute(AsyncWebServer& server);

#endif // ROUTE_HANDLERS_H
