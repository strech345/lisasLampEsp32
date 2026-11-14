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

#define FLASH_GREEN 0x0000FF00
#define FLASH_RED   0x00FF0000

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

// RTC crystal accuracy (ppm) for worst-case drift calculation
static const uint32_t RTC_ACCURACY_PPM = 20;
// Desired lead time before any timed event (alarm / celebration)
static const unsigned long EVENT_LEAD_TIME_MS = 5UL * 60 * 1000;

void onStateUpdatedFromWifi(StateChangeType type, void* data);
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value);
void checkAndApplyColorMode(const FullConfig& config);
void checkLampState();
void onShortPress();
void updateLed();
void checkToGoSleep();
void enterDeepSleep(uint64_t sleepTimeMs);
void enterPowerSaveMode(uint64_t sleepTimeMs);

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
    // Apply color mode on startup
    checkAndApplyColorMode(appConfig);

    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    // nvs_flash_erase(); // Löscht die gesamte NVS-Partition
    // nvs_flash_init();  // Initialisiert sie leer neu

    // 3. WiFi-Zugangsdaten explizit löschen (viele Libraries puffern diese noch)
    // WiFi.disconnect(true, true);

    warningsEnabled = loadWarningsEnabled();
    deepSleepEnabled = loadDeepSleepEnabled();

    setAlarms(appConfig.alarms); // Restore alarms and calculate next trigger

    // Initialize route handlers with state and get routes
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi();
    // startSimpleAP();
    /*  WiFi.disconnect(true); // Clear stored WiFi credentials
     WiFi.persistent(false);
     WiFi.mode(WIFI_AP);
     delay(100);
     WiFi.softAP("Lisas_Lamp_Setup", "myPassword2", 1, false, 1); */

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
        //  performWiFiTest(true);
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

// Callback function to handle rotary encoder events
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value) {
    switch(eventType) {
    case RotaryEncoderEventType::ShortClick:
        serialPrint("ShortClick Event!");
        onShortPress();
        break;
    case RotaryEncoderEventType::LongClick:
        serialPrint("LongClick Event!");
        logEvent("WiFi started (long press)");
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
        logEvent("Factory reset triggered");
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
    if(isCelebrationActive()) {
        lampState = LAMP_STATE_CELEBRATION;
        return;
    }
    if(lampState == LAMP_STATE_SLEEP) {
        return;
    }

    // Persistent system warnings: only active when lamp would otherwise be in DEFAULT
    if(warningsEnabled) {
        time_t now = time(nullptr);
        if(now < 1577836800L) { // no valid time (before 2020)
            lampState = LAMP_STATE_SYS_ERROR;
            return;
        }
        unsigned long sinceSyncMs;
        if(wifiTracker.lastSucceededTestTime == 0) {
            // Never NTP-synced — stale after 30 min grace period
            sinceSyncMs = (millis() > 30UL * 60 * 1000) ? (25UL * 60 * 60 * 1000) : 0;
        } else {
            sinceSyncMs = millis() - wifiTracker.lastSucceededTestTime;
        }
        if(sinceSyncMs > 24UL * 60 * 60 * 1000) {
            lampState = LAMP_STATE_SYS_WARNING;
            return;
        }
    }

    lampState = LAMP_STATE_DEFAULT;
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
    if(lampState != lastLampState) {
        char buf[52];
        snprintf(buf, sizeof(buf), "State: %s -> %s", lampStateName(lastLampState), lampStateName(lampState));
        logEvent(buf);
    }
    if(lampState == LAMP_STATE_DEFAULT && lastLampState != LAMP_STATE_DEFAULT) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        setBrightnessLevel(appConfig.brightnessMode);
        checkAndApplyColorMode(appConfig);
    }
    if(lampState == LAMP_STATE_CELEBRATION && lastLampState != LAMP_STATE_CELEBRATION) {
        startCelebrationEffect();
    }
    if(lampState == LAMP_STATE_ERROR && lastLampState != LAMP_STATE_ERROR) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        setLedColor(0x00FF0000); // blink red until time is synced
    }
    if(lampState == LAMP_STATE_WARNING && lastLampState != LAMP_STATE_WARNING) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        setLedColor(0x00FFFF00); // blink yellow until wifi is not used
    }
    /* if(lampState == LAMP_STATE_SUCCESS && lastLampState !=
    LAMP_STATE_SUCCESS) { setBrightnessLevel(appConfig.brightnessMode || 7);
        setLedColor(0, 255, 0); // blink green until wifi is not used
    } */
    if(lampState == LAMP_STATE_SYS_ERROR && lastLampState != LAMP_STATE_SYS_ERROR) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        startSinglePixelBlink(0x00FF0000); // red
    }
    if(lampState == LAMP_STATE_SYS_WARNING && lastLampState != LAMP_STATE_SYS_WARNING) {
        setBrightnessLevel(appConfig.brightnessMode ? appConfig.brightnessMode : 7);
        startSinglePixelBlink(0x00FFFF00); // yellow
    }
    if(lampState == LAMP_STATE_ALARM && lastLampState != LAMP_STATE_ALARM) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        checkAndApplyColorMode(appConfig);
    }
    if(lampState == LAMP_STATE_SLEEP && lastLampState != LAMP_STATE_SLEEP) {
        setBrightnessLevel(0);
    }
    lastLampState = lampState;

    if(s_pendingFlash) {
        flashLeds(s_pendingFlash);
        s_pendingFlash = 0;
    }

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
        s_pendingFlash = FLASH_GREEN;
        return;
    }
    if(lampState == LAMP_STATE_ALARM) {
        serialPrint("stop alarm");
        lampState = LAMP_STATE_DEFAULT;
        stopActiveAlarm();
        s_pendingFlash = FLASH_RED;
        return;
    }
    if(lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("stop good night");
        lampState = LAMP_STATE_DEFAULT;
        stopGoodNightMode();
        s_pendingFlash = FLASH_RED;
        return;
    }
    if(lampState == LAMP_STATE_CELEBRATION) {
        serialPrint("stop celebration");
        lampState = LAMP_STATE_DEFAULT;
        stopCelebration();
        s_pendingFlash = FLASH_RED;
        return;
    }
    serialPrint("activate good night");
    activateGoodNightMode();
    s_pendingFlash = FLASH_GREEN;
}

// Worst-case RTC drift for a given sleep duration.
static unsigned long calcDriftMs(unsigned long sleepMs) {
    return (unsigned long)((uint64_t)sleepMs * RTC_ACCURACY_PPM / 1000000ULL);
}

void checkToGoSleep() {
    static unsigned long lastCheckMs = 0;
    if(!isTimeForAction(&lastCheckMs, 10 * 1000))
        return;

    if(!deepSleepEnabled)
        return;

    if(lampState != LAMP_STATE_DEFAULT && lampState != LAMP_STATE_SLEEP)
        return;

    if(lampState == LAMP_STATE_DEFAULT && appConfig.brightnessMode != 0)
        return;

    long msToNextAlarm = getMillisToNextAlarm();
    long msToNextCelebration = getMillisToNextCelebration();

    if(msToNextAlarm == 0) // alarm currently active
        return;

    if(isWiFiActive())
        return;

    // Base: sleep until next periodic WiFi sync (no lead time needed for that)
    unsigned long sleepTimeMs = START_WIFI_AFTER_MS;

    // For timed events: wake up early enough to account for RTC drift + 5 min buffer.
    // Lead time = EVENT_LEAD_TIME_MS + worst-case drift over the full time to event.
    // We use the full time-to-event for the drift (conservative upper bound).
    auto applyEventLead = [&](long msToEvent) {
        if(msToEvent <= 0)
            return;
        unsigned long raw = (unsigned long)msToEvent;
        unsigned long lead = EVENT_LEAD_TIME_MS + calcDriftMs(raw);
        if(raw > lead) {
            unsigned long candidate = raw - lead;
            if(candidate < sleepTimeMs)
                sleepTimeMs = candidate;
        }
    };

    applyEventLead(msToNextAlarm);
    applyEventLead(msToNextCelebration);

    serialPrint("Deep sleep candidate: " + String(sleepTimeMs / 1000) + "s"
                + " (alarm in " + String(msToNextAlarm / 1000) + "s"
                + ", celebration in " + String(msToNextCelebration / 1000) + "s)");

    // Don't sleep if the window is too short (< 2 min)
    if(sleepTimeMs < 2UL * 60 * 1000)
        return;

    enterDeepSleep(sleepTimeMs);
}

void enterDeepSleep(uint64_t sleepTimeMs) {
    serialPrint("Auto-entering deep sleep (idle) for " + String((unsigned long)(sleepTimeMs / 1000)) + "s");

    // Timer wakeup
    esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);

    // GPIO wakeup for C3 (Rotary Encoder Switch and CLK)
    // GPIO 3 (SW): Active High, Idle Low -> Wake on HIGH
    esp_deep_sleep_enable_gpio_wakeup(1ULL << 3, ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_pulldown_en(GPIO_NUM_3);
    gpio_pullup_dis(GPIO_NUM_3);

    esp_deep_sleep_enable_gpio_wakeup(1ULL << 1, ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_pullup_en(GPIO_NUM_1); // Ensure default pull-up
    gpio_pulldown_dis(GPIO_NUM_1);

    // GPIO 1 (CLK): Dynamic Wakeup
    // Determine current state and wake on change (opposite level)
    /* gpio_pullup_en(GPIO_NUM_1); // Ensure default pull-up
    gpio_pulldown_dis(GPIO_NUM_1);
    delay(10); // Allow pull-up to settle

    // Idle Low -> Wake on HIGH !!!!!!!!!!!!!!!!!
    Serial.println("Rotary Encoder CLK pin is LOW, setting wake on HIGH");
    esp_deep_sleep_enable_gpio_wakeup(1ULL << 1, ESP_GPIO_WAKEUP_GPIO_HIGH); */

    Serial.flush();
    statusLedOff();
    esp_deep_sleep_start();
}


void enterPowerSaveMode(uint64_t sleepTimeMs) {
    serialPrint("Preparing Light Sleep...");

    statusLedOff();

    // 1. WiFi/Modem-Sleep Vorbereitung
    // Es ist ratsam, WiFi kurz "ruhen" zu lassen, damit async_tcp keine aktiven Timer hat
    delay(100);

    // 2. Watchdog umkonfigurieren statt abzuschalten
    // Da deinit fehlschlagen kann (wenn andere Tasks noch registriert sind),
    // erhöhen wir einfach den Timeout auf die Schlafdauer + Puffer.

    // Sicherstellen, dass der aktuelle Task überwacht wird, bevor wir ihn resetten oder rekonfigurieren
    if(esp_task_wdt_add(NULL) != ESP_OK) {
        // Task war evtl. schon hinzugefügt oder Fehler, wir machen trotzdem weiter
    }

    uint32_t wdt_timeout_ms = (uint32_t)sleepTimeMs + 60000; // sleep + 1 Minute Puffer
    esp_task_wdt_config_t sleep_twdt_config = {
        .timeout_ms = wdt_timeout_ms,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&sleep_twdt_config);
    esp_task_wdt_reset(); // Watchdog füttern bevor wir schlafen

    // 3. Schlaf-Konfiguration
    esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);
    gpio_wakeup_enable(GPIO_NUM_3, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(GPIO_NUM_1, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    Serial.println("Entering Light Sleep now...");
    Serial.flush();

    // 4. In den Light Sleep gehen
    esp_light_sleep_start();

    // --- HIER WACHT ER WIEDER AUF ---

    // 5. Watchdog wieder normalisieren
    esp_task_wdt_config_t normal_twdt_config = {
        .timeout_ms = 30000,
        .idle_core_mask = (1 << 0),
        .trigger_panic = true,
    };
    esp_task_wdt_reconfigure(&normal_twdt_config);
    esp_task_wdt_add(NULL); // Sicherstellen, dass wir registriert sind

    // Wakeup Grund prüfen und Status aktualisieren
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if(wakeup_reason == ESP_SLEEP_WAKEUP_GPIO) {
        serialPrint("Wakeup caused by GPIO - Restoring Lamp State");
        lampState = LAMP_STATE_DEFAULT;
        if(appConfig.brightnessMode == 0) {
            appConfig.brightnessMode = 1;
        }
        setBrightnessLevel(appConfig.brightnessMode);
    } else {
        serialPrint("Wakeup caused by Timer/Other: " + String(wakeup_reason));
    }

    statusLedOn();

    serialPrint("Woke up. Watchdog reconfigured.");
}