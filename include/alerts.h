#ifndef ALERTS_H
#define ALERTS_H

#include <Arduino.h>

enum AlertType {
    ALERT_NONE = 0,
    ALERT_ERROR = 1,
    ALERT_WARNING = 2,
    ALERT_OK = 3
};

/**
 * @brief Starts an alert with an optional duration.
 * @param type The type of alert to start.
 * @param durationMs Optional duration in milliseconds. 0 means persistent.
 */
void startAlert(AlertType type, unsigned long durationMs = 0);

/**
 * @brief Stops the current alert.
 */
void stopAlert();

/**
 * @brief Returns the current active alert type.
 */
AlertType getActiveAlertType();

/**
 * @brief Periodically check if a timed alert should be cleared.
 */
void checkAlertState();

#endif // ALERTS_H
