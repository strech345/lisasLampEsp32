#include "lamp_state.h"
#include <Arduino.h>
#include "led.h"
#include "alerts.h"
#include "alarm.h"
#include "good_night.h"
#include "celebration.h"
#include "debug_utils.h"
#include "event_log.h"

#define FLASH_GREEN 0x0000FF00
#define FLASH_RED   0x00FF0000

static LampState* s_lampState = nullptr;
static LampState* s_lastNormalState = nullptr;
static bool* s_warningsEnabled = nullptr;
static FullConfig* s_appConfig = nullptr;
static WiFiTestTracker* s_wifiTracker = nullptr;
static uint32_t* s_pendingFlash = nullptr;

void initLampState(LampState* pLampState, LampState* pLastNormalState, bool* pWarningsEnabled,
                   FullConfig* pAppConfig, WiFiTestTracker* pWifiTracker, uint32_t* pPendingFlash) {
    s_lampState = pLampState;
    s_lastNormalState = pLastNormalState;
    s_warningsEnabled = pWarningsEnabled;
    s_appConfig = pAppConfig;
    s_wifiTracker = pWifiTracker;
    s_pendingFlash = pPendingFlash;
}

void checkAndApplyColorMode(const FullConfig& config) {
    // Always apply color mode (color override is always active)
    RGB colorToApply;
    String modeName;

    // Check colorMode to determine which color to use
    uint32_t colorHex = 0;
    switch(config.colorMode) {
    case 0:                    // Cool White
        colorHex = 0xFF000080; // White channel + some Blue
        modeName = "Cool White";
        break;
    case 1:                    // Neutral White
        colorHex = 0xFF503000; // White channel + slight Red/Green for better Neutral
        modeName = "Neutral White";
        break;
    case 2:                    // Warm White
        colorHex = 0xFF804000; // White channel + some Red/Green
        modeName = "Warm White";
        break;
    case 3: // Custom
        colorToApply = config.color;
        // Convert RGB struct to uint32_t (White channel 0)
        colorHex = (uint32_t(0) << 24) | (uint32_t(colorToApply.r) << 16) | (uint32_t(colorToApply.g) << 8)
            | uint32_t(colorToApply.b);
        modeName = "Custom";
        break;
    default: // Default to Neutral White
        colorHex = 0xFF000000;
        modeName = "Neutral White (default)";
        break;
    }

    setLedColor(colorHex);
    setAnimationMode(config.animationMode);
    setAnimationSpeed(config.animationSpeed);
    serialPrint("LED color set to " + modeName + " (Hex: " + String(colorHex, HEX) + ")");
}

void checkLampState() {
    switch(getActiveAlertType()) {
    case ALERT_ERROR:
        *s_lampState = LAMP_STATE_ERROR;
        return;
    case ALERT_WARNING:
        *s_lampState = LAMP_STATE_WARNING;
        return;
    case ALERT_OK:
        *s_lampState = LAMP_STATE_SUCCESS;
        return;
    }
    if(isAlarmActive()) {
        *s_lampState = LAMP_STATE_ALARM;
        return;
    }
    if(*s_lampState == LAMP_STATE_ALARM) {
        serialPrint("change from alarm to sleep mode");
        *s_lampState = LAMP_STATE_SLEEP;
        return;
    }
    if(isGoodNightModeActive()) {
        *s_lampState = LAMP_STATE_GOOD_NIGHT;
        return;
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("Good night mode ended, returning to default lamp state");
        *s_lampState = LAMP_STATE_SLEEP;
        return;
    }
    if(isCelebrationActive()) {
        *s_lampState = LAMP_STATE_CELEBRATION;
        return;
    }
    if(*s_lampState == LAMP_STATE_SLEEP) {
        return;
    }

    // Persistent system warnings: only active when lamp would otherwise be in DEFAULT
    if(*s_warningsEnabled) {
        time_t now = time(nullptr);
        if(now < 1577836800L) { // no valid time (before 2020)
            *s_lampState = LAMP_STATE_SYS_ERROR;
            return;
        }
        unsigned long sinceSyncMs;
        if(s_wifiTracker->lastSucceededTestTime == 0) {
            // Never NTP-synced — stale after 30 min grace period
            sinceSyncMs = (millis() > 30UL * 60 * 1000) ? (25UL * 60 * 60 * 1000) : 0;
        } else {
            sinceSyncMs = millis() - s_wifiTracker->lastSucceededTestTime;
        }
        if(sinceSyncMs > 24UL * 60 * 60 * 1000) {
            *s_lampState = LAMP_STATE_SYS_WARNING;
            return;
        }
    }

    *s_lampState = LAMP_STATE_DEFAULT;
}

static const char* lampStateName(LampState s) {
    switch(s) {
    case LAMP_STATE_DEFAULT:     return "Default";
    case LAMP_STATE_ALARM:       return "Alarm";
    case LAMP_STATE_GOOD_NIGHT:  return "Good Night";
    case LAMP_STATE_ERROR:       return "Error";
    case LAMP_STATE_SUCCESS:     return "Success";
    case LAMP_STATE_WARNING:     return "Warning";
    case LAMP_STATE_SLEEP:       return "Sleep";
    case LAMP_STATE_CELEBRATION: return "Celebration";
    case LAMP_STATE_SYS_ERROR:   return "SysError: no time";
    case LAMP_STATE_SYS_WARNING: return "SysWarning: stale NTP";
    default:                     return "Unknown";
    }
}

void updateLed() {
    static LampState lastLampState;
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // State changes
    if(*s_lampState != lastLampState) {
        char buf[52];
        snprintf(buf, sizeof(buf), "State: %s -> %s", lampStateName(lastLampState), lampStateName(*s_lampState));
        logEvent(buf);
    }
    if(*s_lampState == LAMP_STATE_DEFAULT && lastLampState != LAMP_STATE_DEFAULT) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        setBrightnessLevel(s_appConfig->brightnessMode);
        checkAndApplyColorMode(*s_appConfig);
    }
    if(*s_lampState == LAMP_STATE_CELEBRATION && lastLampState != LAMP_STATE_CELEBRATION) {
        startCelebrationEffect();
    }
    if(*s_lampState == LAMP_STATE_ERROR && lastLampState != LAMP_STATE_ERROR) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        setLedColor(0x00FF0000); // blink red until time is synced
    }
    if(*s_lampState == LAMP_STATE_WARNING && lastLampState != LAMP_STATE_WARNING) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        setLedColor(0x00FFFF00); // blink yellow until wifi is not used
    }
    if(*s_lampState == LAMP_STATE_SYS_ERROR && lastLampState != LAMP_STATE_SYS_ERROR) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        startSinglePixelBlink(0x00FF0000); // red
    }
    if(*s_lampState == LAMP_STATE_SYS_WARNING && lastLampState != LAMP_STATE_SYS_WARNING) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        startSinglePixelBlink(0x00FFFF00); // yellow
    }
    if(*s_lampState == LAMP_STATE_ALARM && lastLampState != LAMP_STATE_ALARM) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        checkAndApplyColorMode(*s_appConfig);
    }
    if(*s_lampState == LAMP_STATE_SLEEP && lastLampState != LAMP_STATE_SLEEP) {
        setBrightnessLevel(0);
    }
    lastLampState = *s_lampState;

    if(*s_pendingFlash) {
        flashLeds(*s_pendingFlash);
        *s_pendingFlash = 0;
    }

    // Stay same
    if(currentTime - lastCheck < 5000) {
        return;
    }
    lastCheck = currentTime;
    serialPrint("Lamp state check: " + String(*s_lampState));

    if(*s_lampState == LAMP_STATE_ALARM) {
        setBrightnessLevel(getAlarmBrightness(s_appConfig->alarmDuration));
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        setBrightnessLevel(getGoodNightBrightness(s_appConfig->brightnessMode, s_appConfig->goodNightDuration));
    }
}

void onShortPress() {
    if(getActiveAlertType() != ALERT_NONE) {
        serialPrint("Clearing alert via button");
        stopAlert();
        *s_lampState = *s_lastNormalState;
        return;
    }
    if(*s_lampState == LAMP_STATE_SLEEP) {
        serialPrint("stopping sleep mode");
        *s_lampState = LAMP_STATE_DEFAULT;
        *s_pendingFlash = FLASH_GREEN;
        return;
    }
    if(*s_lampState == LAMP_STATE_ALARM) {
        serialPrint("stop alarm");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopActiveAlarm();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("stop good night");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopGoodNightMode();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_CELEBRATION) {
        serialPrint("stop celebration");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopCelebration();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    serialPrint("activate good night");
    activateGoodNightMode();
    *s_pendingFlash = FLASH_GREEN;
}
