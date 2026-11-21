#ifndef CLOCK_SYNC_H
#define CLOCK_SYNC_H
// Zeitpunkte der letzten Uhr-Abgleiche je Quelle (für den Systemmonitor).
// Absolut: NTP (Internet) und RDS (UKW-Radio) liefern die echte Uhrzeit.
// Relativ: TSF rechnet die Uhrzeit aus den Beacon-Zählern bekannter WLANs hoch.
// Die Zeitpunkte liegen im RTC-Speicher: Sie überstehen den Deep Sleep, nicht aber Reset/Stromausfall.

#include <Arduino.h>
#include <ArduinoJson.h>

enum ClockSyncSource : uint8_t { CLOCK_SYNC_NTP = 0, CLOCK_SYNC_RDS = 1, CLOCK_SYNC_TSF = 2 };

// Direkt nach settimeofday() aufrufen: merkt sich die (neue) Uhrzeit als Zeitpunkt des Abgleichs
void noteClockSync(ClockSyncSource source);
// Zeitpunkt (Unix-Sekunden) des letzten Abgleichs dieser Quelle; 0 = seit dem Einschalten noch nie
time_t lastClockSyncAt(ClockSyncSource source);

// Füllt das "clock"-Objekt für /get_system_info: now, ntpAt, rdsAt, tsfAt (Unix-Sekunden, 0 = noch nie),
// lastSyncOk, staActive, nextSyncInMs (0 = läuft gerade, -1 = kein Heimnetz)
void fillClockSyncJson(JsonObject obj);

#endif // CLOCK_SYNC_H
