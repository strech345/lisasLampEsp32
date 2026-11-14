#include <AiEsp32RotaryEncoder.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <WS2812FX.h>

#include "alarm.h"
#include "button.h"
#include "debug_utils.h" // For serialPrint
#include "route_handlers.h"
#include "good_night.h"
#include "led.h"
#include "store.h"
#include "types.h"
#include "preferences_utils.h"
#include "wifi_controller.h"
#include <system_utils.h>
#include <esp_sleep.h>
#include <esp_task_wdt.h>
#include "alerts.h"
#include "celebration.h"
#include "event_log.h"
#include <soc/rtc.h>
#include <nvs_flash.h>
#include "lamp_state.h"
#include "sleep_manager.h"

#define DEBUG

static std::vector<Route> apRoutes;
static uint32_t s_pendingFlash = 0; // set by button handlers, applied in updateLed()

static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;
static LampState lastNormalState = LAMP_STATE_DEFAULT;
static bool warningsEnabled = true;
static bool deepSleepEnabled = true;

void onStateUpdatedFromWifi(StateChangeType type, void* data);

void setup() {
    //  setCpuFrequencyMhz(80);
#ifdef DEBUG
    // Give the USB CDC time to enumerate and the monitor time to connect
    // delay(2000);

    Serial.begin(115200);
    // Wait for USB CDC to connect (max 5s)
    unsigned long startWait = millis();
    while(!Serial && millis() - startWait < 5000) {
        delay(10);
    }
    // Extra safety delay to let the host terminal catch up
    delay(1000);

    Serial.println("\n\n");
    Serial.println("----------------------------------------");
    Serial.println("   ESP32-C3 STATUS: SERIAL CONNECTED    ");
    Serial.println("----------------------------------------");
    Serial.println("Serial connected or timeout reached");
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

    serialPrint("RTC Clock Source Frequency: " + String(rtc_clk_slow_freq_get_hz()) + " Hz");

    ledInit();
    statusLedOn();

    initLampState(&lampState, &lastNormalState, &warningsEnabled, &appConfig, &wifiTracker, &s_pendingFlash);
    initSleepManager(&deepSleepEnabled, &lampState, &appConfig);
    initButtonCallbacks(&appConfig, &lampState, &wifiTracker);

    // Apply color mode on startup
    checkAndApplyColorMode(appConfig);

    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    warningsEnabled = loadWarningsEnabled();
    deepSleepEnabled = loadDeepSleepEnabled();

    setAlarms(appConfig.alarms); // Restore alarms and calculate next trigger

    // Initialize route handlers with state and get routes
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi();

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_GPIO:
        lampState = LAMP_STATE_DEFAULT;
        appConfig.brightnessMode = 1;
        serialPrint("Wakeup caused by GPIO (Rotary Encoder)");
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

    struct tm timeinfo;
    if(getLocalTime(&timeinfo, 0)) {
        serialPrint("Current system time: " + String(asctime(&timeinfo)));
    } else {
        time_t saved = loadLastKnownTime();
        if(saved > 0) {
            struct timeval tv = { .tv_sec = saved, .tv_usec = 0 };
            settimeofday(&tv, nullptr);
            serialPrint("Restored last known time from NVS: " + String((unsigned long)saved));
        } else {
            serialPrint("Current system time: Not synchronized yet, no saved time");
        }
    }
    logEvent("Boot");
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

void onStateUpdatedFromWifi(StateChangeType type, void* data) {
    switch(type) {
    case STATE_CHANGE_CONFIG: {
        const FullConfig* newConfig = static_cast<const FullConfig*>(data);
        FullConfig tempConfig = *newConfig;
        appConfig = tempConfig;
        checkAndApplyColorMode(appConfig);
        setBrightnessLevel(appConfig.brightnessMode);
        setAlarms(appConfig.alarms);
        saveFullConfig(appConfig, true);
        logEvent("Config updated via WiFi");
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
        logEvent("System settings updated via WiFi");
        serialPrint("System settings updated via WiFi controller generic callback.");
        break;
    }
    case STATE_CHANGE_WARNINGS: {
        warningsEnabled = *static_cast<bool*>(data);
        saveWarningsEnabled(warningsEnabled);
        logEvent(warningsEnabled ? "Warnings enabled" : "Warnings disabled");
        serialPrint(String("Warnings ") + (warningsEnabled ? "enabled" : "disabled"));
        break;
    }
    case STATE_CHANGE_DEEP_SLEEP: {
        deepSleepEnabled = *static_cast<bool*>(data);
        saveDeepSleepEnabled(deepSleepEnabled);
        logEvent(deepSleepEnabled ? "Deep sleep enabled" : "Deep sleep disabled");
        serialPrint(String("Deep sleep ") + (deepSleepEnabled ? "enabled" : "disabled"));
        break;
    }
    }
}
