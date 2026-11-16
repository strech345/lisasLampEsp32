#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H
// Zeitsteuerungs-Hilfsfunktionen: nicht-blockierende Intervall- und Timeout-Verwaltung.

// Gibt true zurück wenn intervalMs seit dem letzten Aufruf vergangen sind und aktualisiert *lastRunTime
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs);

enum ProcessStatus {
    PROCESS_INACTIVE,   // Timer läuft nicht
    PROCESS_RUNNING,    // Timer läuft, Timeout noch nicht erreicht
    PROCESS_TIMED_OUT,  // Timeout abgelaufen
    PROCESS_STARTING    // Gerade gestartet (erster Aufruf mit startSignal=true)
};

// Verwaltet einen nicht-blockierenden Prozess mit Timeout
ProcessStatus getTimeoutState(unsigned long* pTimerMs, const bool startSignal,
                              const unsigned long durationMs);
#endif // SYSTEM_UTILS_H
