#ifndef SYSTEM_INFO_H
#define SYSTEM_INFO_H
// Live-Systemdaten (Uptime, CPU-Last, Heap, Dateisystem, Flash, WLAN-Signal, Hotspot-Geräte, Temperatur, Reset-Grund,
// Systemzeit und Uhr-Abgleiche als Objekt "clock", siehe clock_sync.h)
// als JSON für den Endpunkt /get_system_info.

#include <Arduino.h>

// Liefert die Systemdaten als JSON-String (Feldnamen siehe doc/architektur.md).
// Der CPU-Wert ist der Mittelwert seit dem vorigen Aufruf (erster Aufruf: seit Start), -1 wenn nicht messbar.
// "apClients" listet die Geräte am Hotspot (IP, RSSI, "you" = IP gleich requesterIp; keine MAC-Adressen).
// requesterIp: IP des Anfragenden (zum Markieren des eigenen Geräts); 0.0.0.0 wenn unbekannt.
String getSystemInfoJson(IPAddress requesterIp);

#endif // SYSTEM_INFO_H
