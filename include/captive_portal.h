#ifndef CAPTIVE_PORTAL_H
#define CAPTIVE_PORTAL_H

#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <vector>
#include "types.h"

void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP);
void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes);

#endif // CAPTIVE_PORTAL_H
