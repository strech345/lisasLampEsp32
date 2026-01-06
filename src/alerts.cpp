#include "alerts.h"
#include "debug_utils.h"

static AlertType currentAlert = ALERT_NONE;
static unsigned long alertEndTime = 0; // 0 means persistent

void startAlert(AlertType type, unsigned long duration) {
    serialPrint("Starting alert type: " + String(type) + (duration > 0 ? " for " + String(duration) + "ms" : " (persistent)"));
    currentAlert = type;
    alertEndTime = (duration > 0) ? (millis() + duration) : 0;
}

void stopAlert() {
    if (currentAlert != ALERT_NONE) {
        serialPrint("Stopping alert type: " + String(currentAlert));
        currentAlert = ALERT_NONE;
        alertEndTime = 0;
    }
}

AlertType getActiveAlertType() {
    return currentAlert;
}

void checkAlertState() {
    if (currentAlert != ALERT_NONE && alertEndTime > 0) {
        if (millis() >= alertEndTime) {
            serialPrint("Alert duration expired");
            stopAlert();
        }
    }
}
