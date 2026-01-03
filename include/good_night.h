
#ifndef GOOD_NIGHT_H
#define GOOD_NIGHT_H

#include <Arduino.h> // for byte
#include <stdbool.h>

void activateGoodNightMode();
void stopGoodNightMode();
void checkGoodNightMode(uint16_t durationMinutes);
bool isGoodNightModeActive();
byte getGoodNightBrightness(byte startBrightness, uint16_t durationMinutes);

#endif // GOOD_NIGHT_H
