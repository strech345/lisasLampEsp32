// Captive-Portal-Setup — Implementierung
// POST-Bodies werden über _tempObject gepuffert weil ESPAsyncWebServer sie stückweise liefert.
// Alle POST-Routen: nur Content-Type application/json (sonst 415), Origin muss zum Host passen
// (sonst 403, CSRF-Schutz), Body maximal 4096 Bytes (sonst 413).
#include "captive_portal.h"
#include <Arduino.h>
#include "route_handlers.h"
#include "debug_utils.h"
#include "wifi_controller.h"

// Zählt eine Anfrage als Bedienung (verlängert die Hotspot-Laufzeit). Ausgenommen sind die
// Endpunkte, die die geöffnete Weboberfläche automatisch alle paar Sekunden abfragt — sonst liefe der
// Hotspot ewig, solange irgendwo ein Tab offen ist. Captive-Portal-Prüfungen der Handys laufen nicht
// über die App-Routen und zählen daher ebenfalls nicht.
static void markActivity(const char* uri) {
    if(strcmp(uri, "/ping") == 0 || strcmp(uri, "/get_event_log") == 0 || strcmp(uri, "/get_rds_clock") == 0)
        return;
    noteWebActivity();
}

// Maximale Größe eines POST-Bodys in Bytes
static const size_t MAX_BODY_SIZE = 4096;

// Prüft Content-Type und Origin eines POST-Requests. Sendet bei Fehler die Antwort (415/403)
// und gibt false zurück.
static bool checkPostHeaders(AsyncWebServerRequest* request) {
    // Content-Type: nur JSON (ein Zusatz wie "; charset=utf-8" ist erlaubt)
    String ct = request->contentType();
    ct.toLowerCase();
    if(!ct.startsWith("application/json")) {
        request->send(415, "application/json",
                      "{\"status\":\"error\",\"message\":\"Nur Content-Type application/json erlaubt.\"}");
        return false;
    }
    // Origin (falls vorhanden) muss dem Host-Header entsprechen -> Schutz vor Cross-Site-Requests
    if(request->hasHeader("Origin")) {
        String origin = request->header("Origin");
        int schemeEnd = origin.indexOf("://");
        String originHost = (schemeEnd >= 0) ? origin.substring(schemeEnd + 3) : origin;
        originHost.toLowerCase();
        String host = request->host();
        host.toLowerCase();
        if(originHost != host) {
            request->send(403, "application/json",
                          "{\"status\":\"error\",\"message\":\"Origin passt nicht zum Host.\"}");
            return false;
        }
    }
    return true;
}

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
            // stückweise liefert; bei leerem Body (Content‑Length == 0) muss der
            // Handler bereits im Request‑Callback aufgerufen werden, sonst bekommt
            // der Client nie eine Antwort.
            server.on(
                r.uri, r.method,
                // Request‑Callback – wird sofort ausgeführt, wenn kein Body kommt
                [r](AsyncWebServerRequest* request) {
                    if(request->contentLength() == 0 && checkPostHeaders(request)) {
                        markActivity(r.uri);
                        r.handler(request, String(""));
                    }
                },
                NULL,
                // Body‑Callback – wird nur bei vorhandenem Body aufgerufen
                [r](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if(index == 0) {
                        // Header- und Größenprüfung einmalig beim ersten Datenblock
                        if(total > MAX_BODY_SIZE) {
                            request->send(413, "application/json",
                                          "{\"status\":\"error\",\"message\":\"Anfrage zu groß (max. 4096 Bytes).\"}");
                            return;
                        }
                        if(!checkPostHeaders(request)) {
                            return;
                        }
                        // malloc statt new: die Bibliothek gibt _tempObject mit free() frei
                        request->_tempObject = malloc(total + 1);
                        if(request->_tempObject == nullptr) {
                            request->send(500, "text/plain", "Buffer Allocation Failed");
                            return;
                        }
                    }

                    uint8_t* buf = (uint8_t*)request->_tempObject;
                    if(buf == nullptr) {
                        return; // Anfrage wurde bereits abgelehnt → restliche Datenblöcke ignorieren
                    }
                    if(index + len > total) { // defensiv: niemals über den Puffer hinaus schreiben
                        free(buf);
                        request->_tempObject = nullptr;
                        return;
                    }
                    memcpy(buf + index, data, len);

                    if(index + len == total) {
                        buf[total] = '\0';
                        String body;
                        body.concat((const char*)buf, total);
                        free(buf);
                        request->_tempObject = nullptr;
                        markActivity(r.uri);
                        r.handler(request, body);
                    }
                });
        } else {
            server.on(r.uri, r.method, [r](AsyncWebServerRequest* request) {
                markActivity(r.uri);
                r.handler(request, String(""));
            });
        }
    }

    // Alle unbekannten URLs zum Portal weiterleiten
    server.onNotFound([localIPURL](AsyncWebServerRequest* request) {
        request->redirect(localIPURL);
        serialPrint(String("onnotfound [Host: ") + request->host() + "][URL: " + request->url() + "] -> redirect to " + localIPURL);
    });
}
