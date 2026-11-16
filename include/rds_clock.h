#ifndef RDS_CLOCK_H
#define RDS_CLOCK_H

#include "types.h"
#include <Arduino.h>

void initRdsClock(const SystemSettings* settings, WiFiTestTracker* tracker);
void rdsClockLoop();
String getRdsStatusJson();

#endif // RDS_CLOCK_H
