#ifndef WIFI_CONTROLLER_H
#define WIFI_CONTROLLER_H

#include <ESPAsyncWebServer.h>
#include <functional>
#include <vector>
#include "types.h"
#include <DNSServer.h>

static const unsigned long STOP_WIFI_AFTER_MS = 1 * 60 * 1000;    // Hotspot: 1 min ohne Gerät → Hotspot aus
static const unsigned long STA_SYNC_TIMEOUT_MS = 1 * 60 * 1000;   // Heimnetz: Abgleich nach 1 min ohne Erfolg abbrechen

void wifiLoop();                  // Muss in loop() aufgerufen werden
void initWiFiController(const SystemSettings& settings, const std::vector<Route>& routes, WiFiTestTracker& tracker);

// Hotspot und Heimnetz sind getrennte Modi, die gleichzeitig laufen dürfen (APSTA):
// withAP=true  → Hotspot an (Web-UI erreichbar), Heimnetz bleibt wie es ist
// withAP=false → Heimnetz für den NTP-Abgleich an, Hotspot bleibt wie er ist
void startWifi(bool withAP = true);
void stopWifi();          // beides aus
void stopAccessPoint();   // nur Hotspot aus; Heimnetz (APSTA → STA) bleibt verbunden
void restartAccessPoint(); // Hotspot neu starten (nur wenn er läuft), Heimnetz unberührt
void restartStaSync();    // Heimnetz mit neuen Daten neu verbinden, Hotspot unberührt
void requestTimeSync();   // Abgleich von Hand: Heimnetz verbinden (falls aus), sonst NTP neu anfragen

void handleClientRequests();  // DNS-Captive-Portal-Anfragen verarbeiten
void checkWifiStart();        // Zeitabgleich übers Heimnetz nach nextSyncIntervalMs(), unabhängig vom Hotspot
unsigned long nextSyncIntervalMs(); // nach Erfolg syncIntervalMin, nach Fehlschlag syncRetryMin (in ms)
bool isStaActive();           // Heimnetz gerade verbunden/im Aufbau (nur während Abgleich oder Test)
bool lastSyncOk();            // letzter Abgleich-Versuch erfolgreich
unsigned long msSinceNtpThisBoot(); // ms seit NTP-Erfolg in dieser Wachphase; ULONG_MAX = keiner
long msUntilNextSync();       // Restzeit bis zum nächsten Abgleich; 0 = läuft gerade, -1 = kein Heimnetz
void checkWifiStop();         // Hotspot und Heimnetz getrennt nach ihren Regeln abschalten
bool isWiFiActive();
void noteWebActivity();       // echte Bedienung der Weboberfläche: Hotspot-Laufzeit (apTimeoutMin) neu starten
uint8_t wifiStatusBlinkCount(); // Status-LED: 0 = WLAN aus, 1 = Heimnetz, 2 = Hotspot, 3 = beides
bool isApWaitingForClient();  // Hotspot an, aber noch kein Gerät verbunden
bool hasWifiClients();        // Gibt true zurück wenn mindestens ein Gerät verbunden ist

void WiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info); // Intern: reagiert auf STA-Verbindungsereignisse
void initClock();             // Zeitzone setzen, Uhr aus NVS falls leer (früh in setup())
void syncTimeWithNTP();       // SNTP-Sync neu starten; Drift wird im Callback berechnet
void updateTelemetryWiFiStatus(); // minütlich: NTP-Alter (24 h) prüfen, alle 30 min Zeit in NVS sichern

// WLAN-Suche und Verbindungstest (Einstellungen → WLAN). request… legt nur einen Auftrag ab (aus dem
// Webserver-Task erlaubt), false = beschäftigt; ausgeführt wird in wifiLoop().
bool requestWifiScan();
bool requestWifiTest(uint8_t slot, const char* ssid, const char* password); // "******" = gespeichertes PW
String getWifiScanJson(); // {"status":"idle|running|done|failed","networks":[{ssid,rssi,secure}]}
String getWifiTestJson(); // {"status":"idle|running|done","result":"ok|wrong_password|not_found|timeout|failed"}

#endif // WIFI_CONTROLLER_H
