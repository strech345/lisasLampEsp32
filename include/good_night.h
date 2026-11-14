
#ifndef GOOD_NIGHT_H
#define GOOD_NIGHT_H

#include <Arduino.h> // for byte
#include <stdbool.h>

void activateGoodNightMode();                                          // Startet den Gute-Nacht-Modus
void stopGoodNightMode();
void checkGoodNightMode(uint16_t durationMinutes);                     // Beendet Modus automatisch nach Ablauf der Dauer
bool isGoodNightModeActive();
byte getGoodNightBrightness(byte startBrightness, uint16_t durationMinutes); // Helligkeit nimmt linear bis 0 ab

#endif // GOOD_NIGHT_H
