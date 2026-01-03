#ifndef ROUTE_HANDLERS_H
#define ROUTE_HANDLERS_H

#include <ESPAsyncWebServer.h>
#include <functional>        // For std::function
#include "wifi_controller.h" // for GenericStateUpdateCallback and StateChangeType
#include "types.h"           // For RGB and Alarm types

// Route handlers implemented by the application (main)
// Handler functions for requests (all now match Route signature)
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

// Initialization function to set up global state for handlers and return routes
std::vector<Route> initRouteHandlers(const FullConfig* config, const SystemSettings* systemSettings,
                                     const WiFiTestTracker* wifiTracker, GenericStateUpdateCallback stateCallback);

// Let main register a callback which gets invoked when a new config is set via /set_config
void setOnStateChangedCallback(GenericStateUpdateCallback cb);

#endif // ROUTE_HANDLERS_H
