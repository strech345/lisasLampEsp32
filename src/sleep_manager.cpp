// Deep-Sleep-Verwaltung — Implementierung
// Schlafdauer wird so gewählt dass Alarm und Feiertagsereignisse trotz RTC-Drift nicht verpasst werden.
#include "sleep_manager.h"
#include "pins.h"
#include <Arduino.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include "led.h"
#include "wifi_controller.h"
#include "alarm.h"
#include "celebration.h"
#include "debug_utils.h"
#include "system_utils.h"
#include "store.h"
#include "rtc_crystal.h"

// Genauigkeit des RTC-Takts in ppm (parts per million), relativ zur Schlafdauer.
// Interner RC-Oszillator (Super Mini hat standardmäßig keinen Quarz): ca. 5 % Toleranz laut
// Datenblatt, kalibriert in der Praxis ca. 1–2 % → 15000 ppm = 1,5 % (5 h Schlaf ≈ 4,5 min Drift).
// Mit externem 32-kHz-Quarz: 100 ppm.
static const uint32_t RTC_ACCURACY_PPM_RC = 15000;
static const uint32_t RTC_ACCURACY_PPM_XTAL = 100;

// Liefert die zum aktuell aktiven RTC-Takt passende Genauigkeit in ppm
static uint32_t rtcAccuracyPpm() {
    return isRtcCrystalActive() ? RTC_ACCURACY_PPM_XTAL : RTC_ACCURACY_PPM_RC;
}

// Vorlaufzeit vor Alarmen/Feiertagen: Sleep endet früher damit kein Ereignis verpasst wird
static const unsigned long EVENT_LEAD_TIME_MS = 5UL * 60 * 1000;

static bool* s_deepSleepEnabled = nullptr;
static LampState* s_lampState = nullptr;
static FullConfig* s_appConfig = nullptr;

// Initialisiert das Modul mit Zeigern auf den globalen Zustand
void initSleepManager(bool* pDeepSleepEnabled, LampState* pLampState, FullConfig* pAppConfig) {
    // Nach dem Aufwachen aus Deep Sleep Pad-Hold der Encoder-Pins lösen (wurde vor dem Schlaf
    // gesetzt, damit der Pullup im Schlaf erhalten bleibt). Sonst würde pinMode() ignoriert.
    gpio_hold_dis((gpio_num_t)PIN_ENCODER_SW);
    gpio_hold_dis((gpio_num_t)PIN_ENCODER_CLK);
    gpio_deep_sleep_hold_dis();

    s_deepSleepEnabled = pDeepSleepEnabled;
    s_lampState = pLampState;
    s_appConfig = pAppConfig;
}

// Berechnet den maximalen RTC-Drift über eine gegebene Schlafdauer
// (z. B. 5 h bei 15000 ppm → 4,5 min; zusätzlich zur festen Vorlaufzeit von 5 min)
static unsigned long calcDriftMs(unsigned long sleepMs) {
    return (unsigned long)((uint64_t)sleepMs * rtcAccuracyPpm() / 1000000ULL);
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

    // Deep Sleep vermeiden, wenn ein Ereignis im Vorlauffenster liegt
    auto preventSleepIfImminent = [&](int64_t msToEvent) {
        if (msToEvent > 0 && msToEvent < (int64_t)EVENT_LEAD_TIME_MS) {
            sleepTimeMs = 0;
        }
    };
    preventSleepIfImminent(msToNextAlarm);
    preventSleepIfImminent(msToNextCelebration);
    if (sleepTimeMs == 0) {
        serialPrint("Deep Sleep wird nicht eingeleitet – bevorstehender Alarm oder Feier");
        return;
    }

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

// Versetzt den ESP32 in Deep Sleep mit Timer-Wakeup und GPIO-Wakeup für Encoder-Taster/-Drehung
void enterDeepSleep(uint64_t sleepTimeMs) {
    serialPrint("Auto-entering deep sleep (idle) for " + String((unsigned long)(sleepTimeMs / 1000)) + "s");

    // Ausstehende (debounced) Config sofort schreiben, sonst geht die letzte Änderung verloren
    flushPendingSave();

    // WS2812 halten ihre Farbe ohne Datenleitung → vor dem Schlaf explizit dunkel schalten
    setBrightnessLevel(0);
    ledUpdate();

    // Timer-Wakeup
    esp_sleep_enable_timer_wakeup(sleepTimeMs * 1000ULL);

    // Encoder-Taster SW (PIN_ENCODER_SW): wird im Betrieb mit INPUT_PULLUP gelesen, gedrückt = LOW.
    // Im Schlaf daher ebenfalls Pullup (kein Pulldown, sonst Pin dauerhaft LOW → Dauer-Wakeup)
    // und Wake bei LOW (= Tastendruck).
    gpio_pulldown_dis((gpio_num_t)PIN_ENCODER_SW);
    gpio_pullup_en((gpio_num_t)PIN_ENCODER_SW);
    gpio_hold_en((gpio_num_t)PIN_ENCODER_SW); // Pad-Konfiguration (Pullup) im Deep Sleep einfrieren
    esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_ENCODER_SW, ESP_GPIO_WAKEUP_GPIO_LOW);

    // Encoder CLK (PIN_ENCODER_CLK): ebenfalls Pullup, Wake bei LOW = Drehbewegung.
    // Liegt CLK in der Rastposition bereits auf LOW, würde der Wakeup sofort auslösen →
    // dann diesen Wakeup-Pin weglassen (Taster und Timer bleiben aktiv).
    gpio_pulldown_dis((gpio_num_t)PIN_ENCODER_CLK);
    gpio_pullup_en((gpio_num_t)PIN_ENCODER_CLK);
    gpio_hold_en((gpio_num_t)PIN_ENCODER_CLK);
    if(digitalRead(PIN_ENCODER_CLK) == HIGH) {
        esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_ENCODER_CLK, ESP_GPIO_WAKEUP_GPIO_LOW);
    } else {
        serialPrint("Encoder-CLK liegt auf LOW – Drehen weckt nicht aus dem Deep Sleep");
    }

    gpio_deep_sleep_hold_en(); // Hold gilt im Deep Sleep global; wird in initSleepManager() gelöst

    Serial.flush();
    statusLedOff();
    esp_deep_sleep_start();
}
