#ifndef ALERTS_H
#define ALERTS_H

#include <Arduino.h>

enum AlertType {
    ALERT_NONE = 0,
    ALERT_ERROR = 1,
    ALERT_WARNING = 2,
    ALERT_OK = 3
};

// Startet einen Alert; durationMs=0 bedeutet dauerhaft (bis stopAlert() aufgerufen wird)
void startAlert(AlertType type, unsigned long durationMs = 0);

// Beendet den aktiven Alert
void stopAlert();

// Gibt den aktuell aktiven Alert-Typ zurück
AlertType getActiveAlertType();

// Beendet einen zeitbegrenzten Alert automatisch wenn die Dauer abgelaufen ist
void checkAlertState();

#endif // ALERTS_H
