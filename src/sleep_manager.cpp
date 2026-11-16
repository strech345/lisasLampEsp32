// Deep-Sleep-Verwaltung — Implementierung
// Schlafdauer wird so gewählt dass Alarm und Feiertagsereignisse trotz RTC-Drift nicht verpasst werden.
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

// Genauigkeit des internen RTC-Oszillators in ppm (parts per million)
// Der ESP32-C3 Super Mini hat keinen externen 32-kHz-Quarz → Drift ~1–2%/Tag
static const uint32_t RTC_ACCURACY_PPM = 20;

// Vorlaufzeit vor Alarmen/Feiertagen: Sleep endet früher damit kein Ereignis verpasst wird
static const unsigned long EVENT_LEAD_TIME_MS = 5UL * 60 * 1000;

static bool* s_deepSleepEnabled = nullptr;
static LampState* s_lampState = nullptr;
static FullConfig* s_appConfig = nullptr;

// Initialisiert das Modul mit Zeigern auf den globalen Zustand
void initSleepManager(bool* pDeepSleepEnabled, LampState* pLampState, FullConfig* pAppConfig) {
    s_deepSleepEnabled = pDeepSleepEnabled;
    s_lampState = pLampState;
    s_appConfig = pAppConfig;
}

// Berechnet den maximalen RTC-Drift über eine gegebene Schlafdauer
static unsigned long calcDriftMs(unsigned long sleepMs) {
    return (unsigned long)((uint64_t)sleepMs * RTC_ACCURACY_PPM / 1000000ULL);
}

// Prüft alle 10 Sekunden ob Deep Sleep eingeleitet werden kann
// Bedingungen: Deep Sleep aktiviert, Lampe aus oder im Sleep-Modus, kein WiFi aktiv,
// kein Alarm läuft gerade, und die nächste Aktion ist noch mindestens 2 Minuten entfernt
void checkToGoSleep() {
    static unsigned long lastCheckMs = 0;
    if(!isTimeForAction(&lastCheckMs, 10 * 1000))
        return;

    if(!*s_deepSleepEnabled)
        return;

    // Nur schlafen wenn Lampe aus ist (Sleep) oder auf Stufe 0 dimmt (Default + Helligkeit 0)
    if(*s_lampState != LAMP_STATE_DEFAULT && *s_lampState != LAMP_STATE_SLEEP)
        return;

    if(*s_lampState == LAMP_STATE_DEFAULT && s_appConfig->brightnessMode != 0)
        return;

    long msToNextAlarm = getMillisToNextAlarm();
    int64_t msToNextCelebration = getMillisToNextCelebration();

    if(msToNextAlarm == 0) // Alarm läuft gerade → nicht schlafen
        return;

    if(isWiFiActive())
        return;

    // Standardmäßig bis zum nächsten periodischen WiFi-Sync schlafen
    unsigned long sleepTimeMs = START_WIFI_AFTER_MS;

    // Für zeitgesteuerte Ereignisse: Vorlaufzeit = 5 Min. + geschätzter RTC-Drift
    // Damit wacht die Lampe rechtzeitig vor dem Alarm auf, auch wenn der RTC driftet
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

    // Nicht schlafen wenn das Fenster kürzer als 2 Minuten ist
    if(sleepTimeMs < 2UL * 60 * 1000)
        return;

    enterDeepSleep(sleepTimeMs);
}

// Versetzt den ESP32 in Deep Sleep mit Timer-Wakeup und GPIO-Wakeup für Encoder-Taster
void enterDeepSleep(uint64_t sleepTimeMs) {
    serialPrint("Auto-entering deep sleep (idle) for " + String((unsigned long)(sleepTimeMs / 1000)) + "s");

    // Timer-Wakeup
    esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);

    // GPIO 3 (Encoder-Taster SW): Pulldown, weckt bei LOW→Druck
    esp_deep_sleep_enable_gpio_wakeup(1ULL << 3, ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_pulldown_en(GPIO_NUM_3);
    gpio_pullup_dis(GPIO_NUM_3);

    // GPIO 1 (Encoder CLK): Pullup, weckt bei LOW→Drehbewegung
    esp_deep_sleep_enable_gpio_wakeup(1ULL << 1, ESP_GPIO_WAKEUP_GPIO_LOW);
    gpio_pullup_en(GPIO_NUM_1);
    gpio_pulldown_dis(GPIO_NUM_1);

    Serial.flush();
    statusLedOff();
    esp_deep_sleep_start();
}
