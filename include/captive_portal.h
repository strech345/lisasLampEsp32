#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H
// Captive-Portal-Setup: konfiguriert DNS-Server und Webserver so dass alle
// Betriebssysteme (iOS, Android, Windows, Firefox) den Portal-Dialog öffnen.

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <vector>
#include "types.h"

// Konfiguriert den DNS-Server (TTL, NoError-Antwort für Portal-Erkennung)
void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP);
// Registriert Captive-Portal-Weiterleitungen, App-Routen und Catch-all auf dem Webserver
void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes);

#endif // CAPTIVE_PORTAL_H
