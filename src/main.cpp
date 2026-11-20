// Einstiegspunkt der Firmware: setup() startet alle Module in der richtigen Reihenfolge,
// loop() ruft alle periodischen Prozesse auf. Die eigentliche Logik steckt in den Modulen
// (Überblick: doc/architektur.md).
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>

#include "alarm.h"
#include "alerts.h"
#include "button.h"
#include "celebration.h"
#include "debug_utils.h"
#include "event_log.h"
#include "good_night.h"
#include "lamp_state.h"
#include "led.h"
#include "rds_clock.h"
#include "route_handlers.h"
#include "rtc_crystal.h"
#include "sleep_manager.h"
#include "store.h"
#include "tsf_clock.h"
#include "types.h"
#include "web_commands.h"
#include "wifi_controller.h"

// --- Globaler Zustand (die Module erhalten Zeiger darauf) ---
static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;

// Wertet den Aufwachgrund aus und setzt den Startzustand.
// Rückgabe: true bei Timer-Wakeup (Lampe bleibt aus).
static bool applyWakeupReason() {
    esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
    switch(reason) {
    case ESP_SLEEP_WAKEUP_GPIO:
        // Nutzer hat den Drehknopf gedrückt oder gedreht → Lampe mit Stufe 1 einschalten
        lampState = LAMP_STATE_DEFAULT;
        appConfig.brightnessMode = 1;
        serialPrint("Wakeup: Drehknopf");
        return false;
    case ESP_SLEEP_WAKEUP_TIMER:
        // Wecker oder WLAN-Sync fällig → Lampe bleibt aus
        lampState = LAMP_STATE_SLEEP;
        serialPrint("Wakeup: Timer");
        return true;
    default:
        serialPrint("Start ohne Deep Sleep (Grund " + String(reason) + ")");
        return false;
    }
}

// Einmalige Initialisierung beim Start
void setup() {
    debugInit();
    if(!LittleFS.begin()) {
        serialPrint("LittleFS mount failed");
    }

    // --- Licht so früh wie möglich: Config laden, Aufwachgrund, LEDs setzen ---
    // (vor WLAN-Init — Kanal-Scan und AP-Start dauern Sekunden; Timer-Wakeup: LEDs bleiben aus)
    loadFullConfigOrDefault(appConfig);
    bool timerWakeup = applyWakeupReason();
    ledInit();
    checkAndApplyColorMode(appConfig);
    setBrightnessLevel(timerWakeup ? 0 : appConfig.brightnessMode);
    serialPrint("Licht gesetzt nach " + String(millis()) + " ms");

    // --- Uhr und Systemeinstellungen ---
    initClock(); // Zeitzone + Uhrzeit aus NVS, vor den Weckern
    loadSystemSettingsOrDefault(systemSettings);
    initRtcCrystal(&systemSettings); // Taktquelle nur beim Boot umschaltbar

    // --- Module mit Zeigern auf den globalen Zustand ---
    initLampState(&lampState, &systemSettings.warningsEnabled, &appConfig, &wifiTracker);
    initSleepManager(&systemSettings.deepSleepEnabled, &lampState, &appConfig);
    initButtonCallbacks(&appConfig, &lampState, &wifiTracker);
    initTsfClock(&systemSettings, &wifiTracker);
    initRdsClock(&systemSettings, &wifiTracker);
    setAlarms(appConfig.alarms); // nächste Auslösezeit berechnen

    // --- Webserver und WLAN ---
    initWebCommands(&appConfig, &systemSettings);
    std::vector<Route> routes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, queueWebCommand);
    initWiFiController(systemSettings, routes, wifiTracker);
    // Beim Start nur das Heim-WLAN (Uhrzeit per NTP), kein Hotspot — den schaltet nur ein langer
    // Druck auf den Drehknopf dazu. Ohne eingetragenes Heim-WLAN bleibt das WLAN aus.
    startWifi(false);

    // --- Drehknopf (Ruhepegel lernen) ---
    initRotaryEncoder(appConfig.brightnessMode, myRotaryEncoderCallback);

    logEvent("Boot");
    serialPrint("Initialized nach " + String(millis()) + " ms");
}

// Hauptschleife — läuft dauerhaft nach setup()
void loop() {
    // --- Aufträge vom Webserver abarbeiten (hier im Arduino-Task, nicht im async_tcp-Task) ---
    processWebCommands();

    // --- Hardware-Eingaben und Verbindungen ---
    wifiLoop();
    tsfClockLoop();
    rdsClockLoop();
    statusLedTick(wifiStatusBlinkCount()); // langsam = WLAN aus, 1× Heimnetz, 2× Hotspot, 3× beides
    rotary_loop();
    ledUpdate(); // nach rotary_loop: Helligkeitsänderung sofort rendern

    // --- Zustandsmaschinen der Lampenmodi prüfen ---
    checkAlarmStates(appConfig.alarmDuration);
    checkAlertState();
    checkGoodNightMode(appConfig.goodNightDuration);
    checkCelebrationLogic();
    checkLampState(); // muss zuletzt laufen: wertet alle Zustände aus

    // --- Ausgaben und Verwaltung ---
    updateLed();
    checkToSave();
    checkToGoSleep();
}
