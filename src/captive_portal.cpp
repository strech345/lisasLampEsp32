#include "captive_portal.h"
#include <Arduino.h>
#include "route_handlers.h"
#include "debug_utils.h"

void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP) {
    dnsServer.setTTL(3600);
    // NoError statt NXDOMAIN: Betriebssysteme erkennen Captive Portals nur wenn der DNS-Server "antwortet"
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
}

void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes) {
    const String localIPURL = String("http://") + localIP.toString();

    // --- Captive-Portal-Routen ---
    // Jedes Betriebssystem prüft nach der WLAN-Verbindung eine bestimmte URL.
    // Diese Antworten lösen den "Anmelden"-Dialog aus.

    // iOS/macOS: "Success" im Antworttext verhindert den Portal-Popup
    server.on("/connecttest.txt", [](AsyncWebServerRequest* request) {
        request->redirect("http://logout.net");
    }); // Windows 11
    server.on("/wpad.dat", [](AsyncWebServerRequest* request) {
        request->send(404);
    }); // 404 verhindert wiederholte Anfragen von Windows 10

    server.on("/generate_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // Android
    server.on("/gen_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // Android (alternativ)
    server.on("/redirect",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // Microsoft
    server.on("/hotspot-detect.html",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // Apple
    server.on("/canonical.html", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // Firefox
    server.on("/success.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->send(200); }); // Firefox
    server.on("/ncsi.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // Windows

    server.on("/favicon.ico", [](AsyncWebServerRequest* request) { request->send(404); });

    // --- Anwendungsrouten registrieren ---
    for(const auto& r : routes) {
        if(r.method == HTTP_POST) {
            // POST: Body wird in _tempObject gepuffert, weil ESPAsyncWebServer ihn
            // stückweise liefert; erst am Ende wird der Handler aufgerufen
            server.on(
                r.uri, r.method,
                [](AsyncWebServerRequest* request) {},
                NULL,
                [r](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if(index == 0) {
                        request->_tempObject = new String("");
                        if(request->_tempObject == nullptr) {
                            request->send(500, "text/plain", "Buffer Allocation Failed");
                            return;
                        }
                    }

                    String* body = (String*)request->_tempObject;
                    if(body) {
                        body->concat((const char*)data, len);
                    }

                    if(index + len == total) {
                        if(body) {
                            r.handler(request, *body);
                            delete body;
                            request->_tempObject = nullptr;
                        } else {
                            request->send(400, "text/plain", "Bad Request: Body lost");
                        }
                    } else if(request->client() && !request->client()->connected()) {
                        // Verbindung abgebrochen → Puffer freigeben
                        if(body) {
                            delete body;
                            request->_tempObject = nullptr;
                        }
                    }
                });
        } else {
            server.on(r.uri, r.method, [r](AsyncWebServerRequest* request) { r.handler(request, String("")); });
        }
    }

    // Alle unbekannten URLs zum Portal weiterleiten
    server.onNotFound([localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
        serialPrint(String("onnotfound [Host: ") + request->host() + "][URL: " + request->url() + "] -> redirect to " + localIPURL);
    });
}
