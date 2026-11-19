#ifndef ALARM_H
#define ALARM_H
// Wecker-Modul: prüft Wochentag/Uhrzeit gegen konfigurierte Alarme,
// verwaltet den aktiven Alarm und berechnet die ansteigende Alarm-Helligkeit.

#include "types.h"
#include <time.h> // For time_t

extern Alarm alarms[MAX_ALARMS];

void setAlarms(const Alarm newAlarms[MAX_ALARMS]); // Alarme übernehmen (laufender Alarm/Auslösetage bleiben erhalten, außer bei Änderung)
void resetAlarms();

int getActiveAlarmIndex();
void setActiveAlarm(int index);
void stopActiveAlarm();

void checkAlarmStates(uint16_t durationMinutes); // Prüft ob ein Alarm ausgelöst werden soll
void calculateMillisToNextAlarm();               // Berechnet ms bis zum nächsten Alarm für Deep Sleep

// Gibt ms bis zum nächsten aktiven Alarm zurück; -1 wenn kein Alarm konfiguriert
long getMillisToNextAlarm();

bool isAlarmActive();
byte getAlarmBrightness(uint16_t durationMinutes); // Helligkeit steigt linear über die Alarmdauer an
#endif // ALARM_H
