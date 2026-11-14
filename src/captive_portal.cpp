#include "captive_portal.h"
#include <Arduino.h>
#include "route_handlers.h"
#include "debug_utils.h"

void setUpDNSServer(DNSServer& dnsServer, const IPAddress& localIP) {
    // Set the TTL for DNS response and start the DNS server
    dnsServer.setTTL(3600);
    // Ensure the DNS reply code is NoError so client OSes treat DNS replies
    // like valid responses
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
}

void setUpWebserver(AsyncWebServer& server, const IPAddress& localIP, const std::vector<Route>& routes) {
    // https://github.com/CDFER/Captive-Portal-ESP32/blob/main/src/main.cpp
    //  Captive Portal
    // Pre reading on the fundamentals of captive portals
    // https://textslashplain.com/2022/06/24/captive-portals/
    // Initialize the runtime URL used by redirect handlers so it's always
    // correct
    const String localIPURL = String("http://") + localIP.toString();

    //======================== Webserver ========================
    // WARNING IOS (and maybe macos) WILL NOT POP UP IF IT CONTAINS THE WORD
    // "Success" https://www.esp8266.com/viewtopic.php?f=34&t=4398 SAFARI (IOS)
    // IS STUPID, G-ZIPPED FILES CAN'T END IN .GZ
    // https://github.com/homieiot/homie-esp8266/issues/476 this is fixed by the
    // webserver serve static function. SAFARI (IOS) there is a 128KB limit to
    // the size of the HTML. The HTML can reference external resources/images
    // that bring the total over 128KB SAFARI (IOS) popup browser has some
    // severe limitations (javascript disabled, cookies disabled)

    // Required
    server.on("/connecttest.txt", [](AsyncWebServerRequest* request) {
        request->redirect("http://logout.net");
    }); // windows 11 captive portal workaround
    server.on("/wpad.dat", [](AsyncWebServerRequest* request) {
        request->send(404);
    }); // Honestly don't understand what this is but a 404 stops win 10 keep
        // calling this repeatedly and panicking the esp32 :)

    // Background responses: Probably not all are Required, but some are. Others
    // might speed things up? A Tier (commonly used by modern systems)
    server.on("/generate_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // android captive portal redirect
    server.on("/gen_204", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // android captive portal redirect (alternative)
    server.on("/redirect",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // microsoft redirect
    server.on("/hotspot-detect.html",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // apple call home
    server.on("/canonical.html", [localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
    }); // firefox captive portal call home
    server.on("/success.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->send(200); }); // firefox captive portal call home
    server.on("/ncsi.txt",
              [localIPURL](AsyncWebServerRequest* request) { request->redirect(localIPURL); }); // windows call home

    // B Tier (uncommon)
    //  server.on("/chrome-variations/seed",[](AsyncWebServerRequest
    //  *request){request->send(200);}); //chrome captive portal call home
    //  server.on("/service/update2/json",[](AsyncWebServerRequest
    //  *request){request->send(200);});
    //  //firefox? server.on("/chat",[](AsyncWebServerRequest
    //  *request){request->send(404);}); //No stop asking Whatsapp, there is no
    //  internet connection server.on("/startpage",[](AsyncWebServerRequest
    //  *request){request->redirect(localIPURL);});

    // return 404 to webpage icon
    server.on("/favicon.ico", [](AsyncWebServerRequest* request) { request->send(404); }); // webpage icon

    // Register provided application routes
    for(const auto& r : routes) {
        if(r.method == HTTP_POST) {
            // For POST routes, capture body and pass to handler
            server.on(
                r.uri, r.method,
                [](AsyncWebServerRequest* request) {
                    // Do nothing in the initial request handler for POST
                    // The body handler will trigger the actual logic
                },
                NULL,
                [r](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if(index == 0) {
                        // Start of body, initialize buffer
                        request->_tempObject = new String("");
                        if(request->_tempObject == nullptr) {
                            request->send(500, "text/plain", "Buffer Allocation Failed");
                            return;
                        }
                    }

                    String* body = (String*)request->_tempObject;
                    if(body) {
                        // Append this chunk efficiently
                        body->concat((const char*)data, len);
                    }

                    if(index + len == total) {
                        // Full body received
                        if(body) {
                            r.handler(request, *body);
                            delete body;
                            request->_tempObject = nullptr;
                        } else {
                            request->send(400, "text/plain", "Bad Request: Body lost");
                        }
                    } else if(request->client() && !request->client()->connected()) {
                        // Client disconnected before full body received, clean up
                        if(body) {
                            delete body;
                            request->_tempObject = nullptr;
                        }
                    }
                });
        } else {
            // For GET routes, pass empty body
            server.on(r.uri, r.method, [r](AsyncWebServerRequest* request) { r.handler(request, String("")); });
        }
    }

    // the catch all
    server.onNotFound([localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
        serialPrint(String("onnotfound [Host: ") + request->host() + "][URL: " + request->url() + "] -> redirect to " + localIPURL);
    });
}
