#ifndef ALARM_H
#define ALARM_H

#include "types.h"
#include <time.h> // For time_t

extern Alarm alarms[MAX_ALARMS];

// Core functions for managing the alarm state machine
Alarm* getAlarms();
void setAlarms(const Alarm newAlarms[MAX_ALARMS]);
void resetAlarms();

int getActiveAlarmIndex();
void setActiveAlarm(int index);
void stopActiveAlarm();

void checkAlarmStates();

bool getAlarmColor(unsigned long currentMillis, RGB& color);

/**
 * @brief Returns the time in milliseconds until the next active alarm event.
 * If no alarm is set, returns -1.
 */
long getMillisToNextAlarm();

bool isAlarmActive();
byte getAlarmBrightness();
bool hasActiveAlarms();
#endif // ALARM_H
