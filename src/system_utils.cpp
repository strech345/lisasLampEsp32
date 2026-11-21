// Zeitsteuerungs-Hilfsfunktionen — Implementierung
#include <Arduino.h>
#include <system_utils.h>

// Gibt true zurück wenn intervalMs seit dem letzten Aufruf vergangen sind; überlaufsicher
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs) {
    unsigned long currentTime = millis();

    // Subtraktion statt Vergleich, damit millis()-Überlauf nach ~49 Tagen korrekt behandelt wird
    if(currentTime - *lastRunTime >= intervalMs) {
        *lastRunTime = currentTime;
        return true;
    }

    return false;
}
