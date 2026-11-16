// Zeitsteuerungs-Hilfsfunktionen — Implementierung
#include <Arduino.h>
#include <system_utils.h>

// Gibt true zurück wenn intervalMs seit dem letzten Aufruf vergangen sind; beim ersten Aufruf immer true
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs) {
    unsigned long currentTime = millis();

    // Subtraktion statt Vergleich, damit millis()-Überlauf nach ~49 Tagen korrekt behandelt wird
    if(currentTime - *lastRunTime >= intervalMs) {
        *lastRunTime = currentTime;
        return true;
    }

    return false;
}

// Verwaltet einen nicht-blockierenden Prozess mit Timeout
ProcessStatus getTimeoutState(unsigned long* pTimerMs, const bool startSignal,
                              const unsigned long durationMs) {
    unsigned long currentTime = millis();

    // --- Start ---
    if(startSignal && *pTimerMs == 0) {
        *pTimerMs = currentTime;
        return PROCESS_STARTING;
    }

    // --- Inaktiv ---
    if(*pTimerMs == 0) {
        return PROCESS_INACTIVE;
    }

    // --- Abgelaufen ---
    if(currentTime - *pTimerMs >= durationMs) {
        *pTimerMs = 0;
        return PROCESS_TIMED_OUT;
    }

    // --- Läuft noch ---
    return PROCESS_RUNNING;
}

