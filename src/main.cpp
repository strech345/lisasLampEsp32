#include <AiEsp32RotaryEncoder.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WS2812FX.h>

#include "alarm.h"
#include "button.h"
#include "debug_utils.h" // For serialPrint
#include "route_handlers.h"
/* #include "state.h" */
#include "good_night.h"
#include "led.h"
#include "store.h"
#include "types.h"
#include "preferences_utils.h"
#include "wifi_controller.h" // New include
#include <system_utils.h>
#include <esp_sleep.h>
#include "alerts.h"
#include "celebration.h"

#define DEBUG

static std::vector<Route> apRoutes;

static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;
static LampState lastNormalState = LAMP_STATE_DEFAULT;

void onStateUpdatedFromWifi(StateChangeType type, void* data);
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value);
void checkAndApplyColorMode(const FullConfig& config);
void checkLampState();
void onShortPress();
void updateLed();
void checkToGoSleep();
void enterDeepSleep(uint64_t sleepTimeMs);

void setup() {
    setCpuFrequencyMhz(80);
#ifdef DEBUG
    Serial.begin(115200);
#endif
    if(!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
    }

    ensureConfigExistsAndResetIfNot();
    if(loadFullConfig(appConfig)) {
        serialPrint("Loaded FullConfig from storage");
    } else {
        serialPrint("Using default configuration");
        appConfig = getDefaultFullConfig();
    }

    ledInit();
    statusLedOn();
    // Apply color mode on startup
    checkAndApplyColorMode(appConfig);

    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    setAlarms(appConfig.alarms); // Restore alarms and calculate next trigger

    // Initialize route handlers with state and get routes
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
    case ESP_SLEEP_WAKEUP_EXT1:
        lampState = LAMP_STATE_DEFAULT;
        appConfig.brightnessMode = 1;
        serialPrint("Wakeup caused by external signal (Rotary Encoder)");
        // If user woke it up, maybe turn it on?
        // For now, it will start in DEFAULT or SLEEP based on saved config.
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        lampState = LAMP_STATE_SLEEP;
        serialPrint("Wakeup caused by timer (Alarm/WiFi)");
        break;
    default:
        serialPrint("Wakeup was not caused by deep sleep: " + String(wakeup_reason));
        break;
    }

    serialPrint("Brightness set to: " + String(appConfig.brightnessMode));
    setBrightnessLevel(appConfig.brightnessMode);
    initRotaryEncoder(appConfig.brightnessMode, myRotaryEncoderCallback);

    serialPrint("Initialized");
}

/**
 * @brief The main loop of the program.
 * Runs repeatedly after setup().
 */
void loop() {
    ledUpdate();
    wifiLoop();
    rotary_loop();

    // effected stated
    checkAlarmStates(appConfig.alarmDuration);
    checkAlertState();
    checkGoodNightMode(appConfig.goodNightDuration);
    checkCelebrationLogic();
    checkLampState(); // lamp state check needs be at last

    // other
    updateLed();
    checkToSave();
    checkToGoSleep();
}

void checkAndApplyColorMode(const FullConfig& config) {
    // Always apply color mode (color override is always active)
    RGB colorToApply;
    String modeName;

    // Check colorMode to determine which color to use
    switch(config.colorMode) {
    case 0: // Cool White
        colorToApply = {173, 216, 230};
        modeName = "Cool White";
        break;
    case 1: // Neutral White
        colorToApply = {255, 255, 255};
        modeName = "Neutral White";
        break;
    case 2: // Warm White
        colorToApply = {255, 200, 150};
        modeName = "Warm White";
        break;
    case 3: // Custom
        colorToApply = config.color;
        modeName = "Custom";
        break;
    default: // Default to Neutral White
        colorToApply = {255, 255, 255};
        modeName = "Neutral White (default)";
        break;
    }

    setLedColor(colorToApply.r, colorToApply.g, colorToApply.b);
    setAnimationMode(config.animationMode);
    setAnimationSpeed(config.animationSpeed);
    serialPrint("LED color set to " + modeName + ": R=" + String(colorToApply.r) + " G=" + String(colorToApply.g)
                + " B=" + String(colorToApply.b));
}

void onStateUpdatedFromWifi(StateChangeType type, void* data) {
    switch(type) {
    case STATE_CHANGE_CONFIG: {
        const FullConfig* newConfig = static_cast<const FullConfig*>(data);
        FullConfig tempConfig = *newConfig;
        // tempConfig.brightnessMode = appConfig.brightnessMode; // Preserve brightness - REMOVED to allow remote update
        appConfig = tempConfig;
        checkAndApplyColorMode(appConfig);
        setBrightnessLevel(appConfig.brightnessMode);
        setAlarms(appConfig.alarms);
        saveFullConfig(appConfig, true);
        serialPrint("Full configuration updated via WiFi controller generic callback.");
        break;
    }
    case STATE_CHANGE_SYSTEM_CONFIG: {
        const SystemSettings* newSettings = static_cast<const SystemSettings*>(data);
        systemSettings = *newSettings;
        saveSystemSettings(systemSettings);
        startAlert(ALERT_OK, 2000); // Trigger green alert for 2s
        stopWifi();
        startWifi();
        //  performWiFiTest(true);
        serialPrint("System settings updated via WiFi controller generic callback.");
        break;
    }
    }
}

// Callback function to handle rotary encoder events
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value) {
    switch(eventType) {
    case RotaryEncoderEventType::ShortClick:
        serialPrint("ShortClick Event!");
        onShortPress();
        break;
    case RotaryEncoderEventType::LongClick:
        serialPrint("LongClick Event!");
        startAlert(ALERT_WARNING, 5000);
        startWifi();
        break;
    case RotaryEncoderEventType::Rotate: {
        // Clamp value to 0-7, cast to byte, and save to config
        int v = value;
        if(v < 0)
            v = 0;
        if(v > 7)
            v = 7;
        appConfig.brightnessMode = (byte)v;
        serialPrint("Brightness mode set to: " + String(appConfig.brightnessMode) + String(" (from ") + String(value)
                    + String(")"));
        saveFullConfig(appConfig, true);
        setBrightnessLevel(appConfig.brightnessMode);
        if(lampState == LAMP_STATE_GOOD_NIGHT) {
            lampState = LAMP_STATE_DEFAULT;
        }
        break;
    }
    case RotaryEncoderEventType::ShortBootClick:
        serialPrint("ShortBootClick Event!");
        // startWifi();
        break;
    case RotaryEncoderEventType::LongBootClick:
        serialPrint("LongBootClick Event!");
        serialPrint("Resetting configuration to defaults...");
        resetFullConfig();
        resetSystemSettings();
        serialPrint("Configuration reset. Restarting ESP...");
        delay(1000); // Give time for serial to flush
        ESP.restart();
        break;
    }
}

void checkLampState() {
    /*  static unsigned long lastCheck = 0;
     unsigned long currentTime = millis(); */

    // Only check every 1 seconds
    /* if(hasActiveAlarms() && !wifiTracker.clockSynced) {
        lampState = LAMP_STATE_ERROR;
        return;
    } */

    switch(getActiveAlertType()) {
    case ALERT_ERROR:
        lampState = LAMP_STATE_ERROR;
        return;
    case ALERT_WARNING:
        lampState = LAMP_STATE_WARNING;
        return;
    case ALERT_OK:
        lampState = LAMP_STATE_SUCCESS;
        return;
    }

    if(isCelebrationActive()) {
        lampState = LAMP_STATE_CELEBRATION;
        return;
    }

    if(isAlarmActive()) {
        lampState = LAMP_STATE_ALARM;
        return;
    }
    if(lampState == LAMP_STATE_ALARM) {
        serialPrint("change from alarm to sleep mode");
        lampState = LAMP_STATE_SLEEP;
        return;
    }
    if(isGoodNightModeActive()) {
        lampState = LAMP_STATE_GOOD_NIGHT;
        return;
    }
    if(lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("Good night mode ended, returning to default lamp state");
        lampState = LAMP_STATE_SLEEP;
        return;
    }
    if(lampState == LAMP_STATE_SLEEP) {
        return;
    }

    lampState = LAMP_STATE_DEFAULT;
}

void updateLed() {
    static LampState lastLampState;
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // State changes
    if(lampState == LAMP_STATE_DEFAULT && lastLampState != LAMP_STATE_DEFAULT) {
        setBrightnessLevel(appConfig.brightnessMode);
        checkAndApplyColorMode(appConfig);
    }
    if(lampState == LAMP_STATE_CELEBRATION && lastLampState != LAMP_STATE_CELEBRATION) {
        startCelebrationEffect();
    }
    if(lampState == LAMP_STATE_ERROR && lastLampState != LAMP_STATE_ERROR) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        setLedColor(255, 0, 0); // blink red until time is synced
    }
    if(lampState == LAMP_STATE_WARNING && lastLampState != LAMP_STATE_WARNING) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        setLedColor(255, 255, 0); // blink yellow until wifi is not used
    }
    /* if(lampState == LAMP_STATE_SUCCESS && lastLampState !=
    LAMP_STATE_SUCCESS) { setBrightnessLevel(appConfig.brightnessMode || 7);
        setLedColor(0, 255, 0); // blink green until wifi is not used
    } */
    if(lampState == LAMP_STATE_ALARM && lastLampState != LAMP_STATE_ALARM) {
        checkAndApplyColorMode(appConfig);
    }
    if(lampState == LAMP_STATE_SLEEP && lastLampState != LAMP_STATE_SLEEP) {
        setBrightnessLevel(0);
    }
    lastLampState = lampState;

    // Stay same
    if(currentTime - lastCheck < 5000) {
        return;
    }
    lastCheck = currentTime;
    Serial.println("Lamp state check: " + String(lampState));

    if(lampState == LAMP_STATE_ALARM) {
        setBrightnessLevel(getAlarmBrightness(appConfig.alarmDuration));
    }
    if(lampState == LAMP_STATE_GOOD_NIGHT) {
        setBrightnessLevel(getGoodNightBrightness(appConfig.brightnessMode, appConfig.goodNightDuration));
    }
    if(lampState == LAMP_STATE_DEFAULT) {
        // setBrightnessLevel(appConfig.brightnessMode);
    }
}

void onShortPress() {
    if(getActiveAlertType() != ALERT_NONE) {
        serialPrint("Clearing alert via button");
        stopAlert();
        lampState = lastNormalState;
        return;
    }
    if(lampState == LAMP_STATE_SLEEP) {
        serialPrint("stopping sleep mode");
        lampState = LAMP_STATE_DEFAULT;
        return;
    }

    if(lampState == LAMP_STATE_ALARM) {
        serialPrint("stop alarm");
        lampState = LAMP_STATE_DEFAULT;
        stopActiveAlarm();
        return;
    }
    if(lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("stop good night");
        lampState = LAMP_STATE_DEFAULT;
        stopGoodNightMode();
        return;
    }
    if(lampState == LAMP_STATE_CELEBRATION) {
        serialPrint("stop celebration");
        lampState = LAMP_STATE_DEFAULT;
        stopCelebration();
        return;
    }
    serialPrint("activate good night");
    // TODO show green preview
    activateGoodNightMode();
}

void checkToGoSleep() {
    static unsigned long lastCheckMs = 0;
    if(!isTimeForAction(&lastCheckMs, 10 * 1000))
        return;

    long msToNextAlarm = getMillisToNextAlarm();
    long msToNextCelebration = getMillisToNextCelebration();

    serialPrint("Millis to next alarm: " + String(msToNextAlarm));
    if(msToNextCelebration > 0) {
        serialPrint("Millis to next celebration: " + String(msToNextCelebration));
    }

    if(lampState != LAMP_STATE_DEFAULT && lampState != LAMP_STATE_SLEEP)
        return;

    if(lampState == LAMP_STATE_DEFAULT && appConfig.brightnessMode != 0)
        return;

    if(msToNextAlarm == 0) // if alarm is active
        return;

    if(isWiFiActive())
        return;

    // Determine how long we can sleep. Start with the WiFi schedule.
    unsigned long sleepTimeMs = START_WIFI_AFTER_MS;

    // If an alarm is sooner, sleep until then.
    if(msToNextAlarm > 0 && (unsigned long)msToNextAlarm < sleepTimeMs) {
        sleepTimeMs = (unsigned long)msToNextAlarm;
    }

    // If a celebration is sooner, sleep until then.
    if(msToNextCelebration > 0 && (unsigned long)msToNextCelebration < sleepTimeMs) {
        sleepTimeMs = (unsigned long)msToNextCelebration;
    }

    // Don't sleep if it's too short (less than 2 minutes)
    if(sleepTimeMs < 2LL * 60LL * 1000LL) {
        return;
    }

    // Deduct 1 minute for a safe wakeup before an alarm or activity
    sleepTimeMs -= 60LL * 1000LL;

    enterDeepSleep(sleepTimeMs);
}

void enterDeepSleep(uint64_t sleepTimeMs) {
    serialPrint("Auto-entering deep sleep (idle) for " + String((unsigned long)(sleepTimeMs / 1000)) + "s");

    // Timer wakeup
    esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);

    // Rotary Encoder Switch (Pin 4) - Wake on LOW
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, 0);

    // Rotary Encoder CLK (Pin 32) - Wake on LOW
    // Use ext1 for this; it can only wake on ALL_LOW or ANY_HIGH
    // Mode ANY_LOW is not supported on original ESP32, but for a single pin, ALL_LOW is the same as wake on LOW.
    esp_sleep_enable_ext1_wakeup(1ULL << 32, ESP_EXT1_WAKEUP_ALL_LOW);

    Serial.flush();
    statusLedOff();
    esp_deep_sleep_start();
}
