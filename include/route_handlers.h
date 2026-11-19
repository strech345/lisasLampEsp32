#ifndef ROUTE_HANDLERS_H
#define ROUTE_HANDLERS_H
// HTTP-Routen-Handler: registriert alle API-Endpunkte, validiert Anfragen und leitet
// Zustandsänderungen als Aufträge über den Callback an main.cpp (Ausführung in loop()) weiter.

#include <ESPAsyncWebServer.h>
#include "wifi_controller.h"
#include "types.h"

// Initialisiert Handler-Zustand und gibt alle Routen zurück
std::vector<Route> initRouteHandlers(const FullConfig* config, const SystemSettings* systemSettings,
                                     const WiFiTestTracker* wifiTracker, GenericStateUpdateCallback stateCallback);

#endif // ROUTE_HANDLERS_H
