#include "alerts.h"
#include "debug_utils.h"
#include "event_log.h"

static AlertType currentAlert = ALERT_NONE;
static unsigned long alertEndTime = 0; // 0 means persistent

static const char* alertTypeName(AlertType t) {
    switch(t) {
    case ALERT_ERROR:   return "Error";
    case ALERT_WARNING: return "Warning";
    case ALERT_OK:      return "OK";
    default:            return "None";
    }
}

void startAlert(AlertType type, unsigned long duration) {
    serialPrint("Starting alert type: " + String(type) + (duration > 0 ? " for " + String(duration) + "ms" : " (persistent)"));
    char buf[48];
    snprintf(buf, sizeof(buf), "Alert start: %s", alertTypeName(type));
    logEvent(buf);
    currentAlert = type;
    alertEndTime = (duration > 0) ? (millis() + duration) : 0;
}

void stopAlert() {
    if (currentAlert != ALERT_NONE) {
        serialPrint("Stopping alert type: " + String(currentAlert));
        char buf[48];
        snprintf(buf, sizeof(buf), "Alert stop: %s", alertTypeName(currentAlert));
        logEvent(buf);
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
