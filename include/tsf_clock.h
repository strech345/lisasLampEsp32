#ifndef TSF_CLOCK_H
#define TSF_CLOCK_H

#include "types.h"
#include <Arduino.h>

void initTsfClock(const SystemSettings* settings, WiFiTestTracker* tracker);
void tsfClockLoop();
void onNtpSynced();
void triggerTsfSnapshot();
String getTsfStatusJson();

#endif // TSF_CLOCK_H
