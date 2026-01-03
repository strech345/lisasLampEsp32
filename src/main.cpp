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

void setup() {
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
    setBrightnessLevel(appConfig.brightnessMode);
    serialPrint("Brightness set to: " + String(appConfig.brightnessMode));
    initRotaryEncoder(appConfig.brightnessMode, myRotaryEncoderCallback);
    // Apply color mode on startup
    checkAndApplyColorMode(appConfig);

    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    // Initialize route handlers with state and get routes
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi();

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
    // performWiFiTest();
    //  checkWifiStop();
    checkAlarmStates(appConfig.alarmDuration);
    checkLampState();
    updateLed();
    checkGoodNightMode(appConfig.goodNightDuration);
    checkToSave();
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
        tempConfig.brightnessMode = appConfig.brightnessMode; // Preserve brightness
        appConfig = tempConfig;
        checkAndApplyColorMode(appConfig);
        setAlarms(appConfig.alarms);
        saveFullConfig(appConfig, true);
        // stay(0, 255, 0, 7, 2000); // green for 2 seconds
        serialPrint("Full configuration updated via WiFi controller generic callback.");
        break;
    }
    case STATE_CHANGE_SYSTEM_CONFIG: {
        const SystemSettings* newSettings = static_cast<const SystemSettings*>(data);
        systemSettings = *newSettings;
        saveSystemSettings(systemSettings);
        stay(0, 255, 0, 7, 2000); // green for 2 seconds
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
        // startWifi();
        ////return;
        onShortPress();
        break;
    case RotaryEncoderEventType::LongClick:
        serialPrint("LongClick Event!");
        stay(255, 255, 0, 7, 2000); // yellow for 2 seconds
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
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // Only check every 1 seconds
    if(currentTime - lastCheck < 1000) {
        return;
    }
    lastCheck = currentTime;

    /* bool hasSomeWarnings = false; // isWifiActiveAndNotUsed(); throw some errors

    if(lampState != LAMP_STATE_WARNING && hasSomeWarnings) {
        serialPrint("AP mode active and no users connected, changing to warning");
        lastNormalState = lampState;
        lampState = LAMP_STATE_WARNING;
        return;
    }
    if(lampState == LAMP_STATE_WARNING && !hasSomeWarnings) {
        serialPrint("AP mode user connected or AP mode inactive, restoring previous state");
        lampState = lastNormalState;
        return;
    } */

    /* if(hasActiveAlarms() && !wifiTracker.clockSynced) {
        lampState = LAMP_STATE_ERROR;
        return;
    } */
    if(lampState == LAMP_STATE_ERROR || lampState == LAMP_STATE_WARNING) {
        return; // stay in error/warning/success until cleared
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
        return; // stay in error/warning/success until cleared
    }

    lampState = LAMP_STATE_DEFAULT;
    /* if(lampState == LAMP_STATE_GOOD_NIGHT || lampState == LAMP_STATE_ALARM) {
        serialPrint("Returning to default lamp state");
        ////////////////////////// GOTO SLEEP MODE //////////////////////////
        lampState = LAMP_STATE_DEFAULT;
        return;
    }
    if(lampState != LAMP_STATE_DEFAULT) {
        setBrightnessLevel(appConfig.brightnessMode);
        lampState = LAMP_STATE_DEFAULT;
        return;
    } */
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
    if(lampState == LAMP_STATE_ERROR || lampState == LAMP_STATE_WARNING) {
        serialPrint("Clearing error/warning state");
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
    serialPrint("activate good night");
    // TODO show green preview
    activateGoodNightMode();
}

void checkToGoSleep() {
    if(lampState != LAMP_STATE_DEFAULT || appConfig.brightnessMode > 0 /* || isWiFiActive() */
       || getMillisToNextAlarm() < 15 * 60 * 1000)
        return;
    // if millis to low stay active

    // otherwise go to sleep
}

/**
 * on press first check if modus is active
 * if true stop modus
 *
 * value change always change brightness
 *
 *
 *
 * button moenu
 * - 0 default (helligkeit)
 *  <> change brightness
    short next mode
 - 1 go sleep
 */