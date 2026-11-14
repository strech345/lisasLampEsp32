#include "sleep_manager.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "led.h"
#include "wifi_controller.h"
#include "alarm.h"
#include "celebration.h"
#include "debug_utils.h"
#include "system_utils.h"

// RTC crystal accuracy (ppm) for worst-case drift calculation
static const uint32_t RTC_ACCURACY_PPM = 20;
// Desired lead time before any timed event (alarm / celebration)
static const unsigned long EVENT_LEAD_TIME_MS = 5UL * 60 * 1000;

static bool* s_deepSleepEnabled = nullptr;
static LampState* s_lampState = nullptr;
static FullConfig* s_appConfig = nullptr;

void initSleepManager(bool* pDeepSleepEnabled, LampState* pLampState, FullConfig* pAppConfig) {
    s_deepSleepEnabled = pDeepSleepEnabled;
    s_lampState = pLampState;
    s_appConfig = pAppConfig;
}

static unsigned long calcDriftMs(unsigned long sleepMs) {
    return (unsigned long)((uint64_t)sleepMs * RTC_ACCURACY_PPM / 1000000ULL);
}

void checkToGoSleep() {
    static unsigned long lastCheckMs = 0;
    if(!isTimeForAction(&lastCheckMs, 10 * 1000))
        return;

    if(!*s_deepSleepEnabled)
        return;

    if(*s_lampState != LAMP_STATE_DEFAULT && *s_lampState != LAMP_STATE_SLEEP)
        return;

    if(*s_lampState == LAMP_STATE_DEFAULT && s_appConfig->brightnessMode != 0)
        return;

    long msToNextAlarm = getMillisToNextAlarm();
    int64_t msToNextCelebration = getMillisToNextCelebration();

    if(msToNextAlarm == 0) // alarm currently active
        return;

    if(isWiFiActive())
        return;

    // Base: sleep until next periodic WiFi sync (no lead time needed for that)
    unsigned long sleepTimeMs = START_WIFI_AFTER_MS;

    // For timed events: wake up early enough to account for RTC drift + 5 min buffer.
    // Lead time = EVENT_LEAD_TIME_MS + worst-case drift over the full time to event.
    // We use the full time-to-event for the drift (conservative upper bound).
    auto applyEventLead = [&](int64_t msToEvent) {
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

    Serial.flush();
    statusLedOff();
    esp_deep_sleep_start();
}

