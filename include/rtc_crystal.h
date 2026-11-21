#ifndef RTC_CRYSTAL_H
#define RTC_CRYSTAL_H
// RTC-Quarz-Extension: schaltet beim Boot auf einen externen 32.768-kHz-Uhrenquarz um
// (GPIO0 = XTAL_32K_P, GPIO1 = XTAL_32K_N). Prüft via Kalibrierungsmessung ob der
// Quarz tatsächlich schwingt und fällt sicher auf den internen RC-Oszillator zurück.

#include "types.h"
#include <Arduino.h>

// Initialisiert: falls enabled, Quarz einschalten und testen; speichert Ergebnis in SystemSettings
// fromDeepSleep = false (Einschalten/Reset): Quarz wird immer auf Vorhandensein geprüft
void initRtcCrystal(SystemSettings* settings, bool fromDeepSleep);

// Liefert true, wenn der externe Quarz erkannt wurde und als RTC-Takt aktiv ist
bool isRtcCrystalActive();

// Liefert Status-JSON für das Frontend
String getRtcCrystalStatusJson();

#endif // RTC_CRYSTAL_H
