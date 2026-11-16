#ifndef RDS_CLOCK_H
#define RDS_CLOCK_H
// RDS-Uhrzeit-Fallback: empfängt RDS-CT-Pakete (Group 4A) über ein FM-Modul
// (RDA5807M per I2C) und stellt damit die Systemzeit ohne Internetzugang ein.

#include "types.h"
#include <Arduino.h>

// Initialisiert I2C-Bus und Power-Pin; letzter bekannter Kanal aus SystemSettings wird bevorzugt
void initRdsClock(const SystemSettings* settings, WiFiTestTracker* tracker);
// Muss in loop() aufgerufen werden: steuert die RDS-Zustandsmaschine (Ein/Aus, Scan, Lock, Zeitsetzung)
void rdsClockLoop();
// Gibt den aktuellen RDS-Status (Kanal, RSSI, Zustand, letzte Synchronisation) als JSON zurück
String getRdsStatusJson();

#endif // RDS_CLOCK_H
