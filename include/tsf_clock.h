#ifndef TSF_CLOCK_H
#define TSF_CLOCK_H
// TSF-Uhrzeit-Fallback: liest Beacon-TSF-Timestamps per Promiscuous-Scan
// und schätzt daraus die aktuelle Unix-Zeit ohne NTP-Verbindung.

#include "types.h"
#include <Arduino.h>

// Initialisiert das Modul mit Systemeinstellungen und WiFi-Tracker; lädt gespeicherte TSF-Referenzwerte
void initTsfClock(const SystemSettings* settings, WiFiTestTracker* tracker);
// Muss in loop() aufgerufen werden: steuert Scan-Start, Beacon-Erfassung und Zeitberechnung
void tsfClockLoop();
// Löst nach einer erfolgreichen NTP-Synchronisation einen neuen TSF-Snapshot aus
void onNtpSynced();
// Nach einem gescheiterten NTP-Versuch: Uhrzeit aus den Bezugspunkten berechnen (WLAN muss noch an sein)
void requestTsfFallback();
// true, solange ein Snapshot/Fallback angefordert ist oder die Beacon-Erfassung läuft (WLAN dann nicht umschalten)
bool isTsfCaptureBusy();
// true, wenn ein Snapshot von Hand erlaubt ist (NTP-Abgleich höchstens 10 min her)
bool isTsfSnapshotAllowed();
// Erzwingt manuell einen neuen TSF-Snapshot (z.B. über Web-API); ohne frische NTP-Zeit wirkungslos
void triggerTsfSnapshot();
// Gibt den aktuellen TSF-Modul-Status als JSON-String zurück
String getTsfStatusJson();

#endif // TSF_CLOCK_H
