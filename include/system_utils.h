#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H
// Zeitsteuerungs-Hilfsfunktionen: nicht-blockierende Intervall-Verwaltung.

// Gibt true zurück wenn intervalMs seit dem letzten Aufruf vergangen sind und aktualisiert *lastRunTime
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs);

#endif // SYSTEM_UTILS_H
