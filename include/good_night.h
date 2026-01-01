
#ifndef GOOD_NIGHT_H
#define GOOD_NIGHT_H

#include <Arduino.h> // for byte
#include <stdbool.h>

void activateGoodNightMode();
void stopGoodNightMode();
void checkGoodNightMode();
bool isGoodNightModeActive();
byte getGoodNightBrightness(byte startBrightness);

#endif // GOOD_NIGHT_H
