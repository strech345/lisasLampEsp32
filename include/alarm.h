#ifndef ALARM_H
#define ALARM_H

#include "types.h"
#include <time.h> // For time_t

extern Alarm alarms[MAX_ALARMS];

Alarm* getAlarms();
void setAlarms(const Alarm newAlarms[MAX_ALARMS]); // Alarme übernehmen und nächste Auslösezeit berechnen
void resetAlarms();

int getActiveAlarmIndex();
void setActiveAlarm(int index);
void stopActiveAlarm();

void checkAlarmStates(uint16_t durationMinutes); // Prüft ob ein Alarm ausgelöst werden soll
void calculateMillisToNextAlarm();               // Berechnet ms bis zum nächsten Alarm für Deep Sleep

bool getAlarmColor(unsigned long currentMillis, RGB& color, uint16_t durationMinutes);

// Gibt ms bis zum nächsten aktiven Alarm zurück; -1 wenn kein Alarm konfiguriert
long getMillisToNextAlarm();

bool isAlarmActive();
byte getAlarmBrightness(uint16_t durationMinutes); // Helligkeit steigt linear über die Alarmdauer an
bool hasActiveAlarms();
#endif // ALARM_H
