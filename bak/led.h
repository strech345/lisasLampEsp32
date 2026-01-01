#ifndef LED_H
#define LED_H

#include "types.h"

void ledSetup();
void ledUpdateColor(const RANGE modeRanges[]);
void ledUpdateBrightness(const RANGE& brightnessRange);
void ledFireEffect(unsigned long currentMs, const RANGE modeRanges[]);
void ledStartBlinking(int mode, const RANGE modeRanges[]);
void ledShow();
void ledWifiIndicator(unsigned long currentMs);

#endif // LED_H
