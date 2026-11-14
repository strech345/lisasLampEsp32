#include <AiEsp32RotaryEncoder.h>
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

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
#include "alerts.h"
#include "celebration.h"
#include "event_log.h"
#include <soc/rtc.h>
#include "lamp_state.h"
#include "sleep_manager.h"

#define DEBUG

// --- Globale Zustandsvariablen ---
static std::vector<Route> apRoutes;
static uint32_t s_pendingFlash = 0; // wird von Button-Handlern gesetzt, in updateLed() angewendet

static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;
static LampState lastNormalState = LAMP_STATE_DEFAULT;

void onStateUpdatedFromWifi(StateChangeType type, void* data);

// Einmalige Initialisierung beim Start
void setup() {
#ifdef DEBUG
    Serial.begin(115200);
    // Warte bis USB-CDC bereit ist (max. 5 Sekunden)
    unsigned long startWait = millis();
    while(!Serial && millis() - startWait < 5000) {
        delay(10);
    }
    // Kurze Pause damit das Terminal die ersten Ausgaben nicht verliert
    delay(1000);

    Serial.println("\n\n");
    Serial.println("----------------------------------------");
    Serial.println("   ESP32-C3 STATUS: SERIAL CONNECTED    ");
    Serial.println("----------------------------------------");
    Serial.println("Serial connected or timeout reached");
#endif

    // --- Dateisystem ---
    if(!LittleFS.begin()) {
        Serial.println("LittleFS mount failed");
    }

    // --- Konfiguration laden ---
    ensureConfigExistsAndResetIfNot();
    if(loadFullConfig(appConfig)) {
        serialPrint("Loaded FullConfig from storage");
    } else {
        serialPrint("Using default configuration");
        appConfig = getDefaultFullConfig();
    }

    serialPrint("RTC Clock Source Frequency: " + String(rtc_clk_slow_freq_get_hz()) + " Hz");

    // --- LED initialisieren ---
    ledInit();
    statusLedOn();

    // --- Systemeinstellungen laden ---
    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    // --- Module mit Zeigern auf globalen Zustand initialisieren ---
    initLampState(&lampState, &lastNormalState, &systemSettings.warningsEnabled, &appConfig, &wifiTracker, &s_pendingFlash);
    initSleepManager(&systemSettings.deepSleepEnabled, &lampState, &appConfig);
    initButtonCallbacks(&appConfig, &lampState, &wifiTracker);

    checkAndApplyColorMode(appConfig);
    setAlarms(appConfig.alarms); // Alarme wiederherstellen und nächste Auslösezeit berechnen

    // --- WiFi und Webserver starten ---
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi();

    // --- Aufwachgrund nach Deep-Sleep auswerten ---
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_GPIO:
        // Nutzer hat den Encoder gedrückt → Lampe einschalten
        lampState = LAMP_STATE_DEFAULT;
        appConfig.brightnessMode = 1;
        serialPrint("Wakeup caused by GPIO (Rotary Encoder)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        // Timer-Wakeup: Alarm oder WiFi-Sync fällig → Lampe bleibt aus
        lampState = LAMP_STATE_SLEEP;
        serialPrint("Wakeup caused by timer (Alarm/WiFi)");
        break;
    default:
        serialPrint("Wakeup was not caused by deep sleep: " + String(wakeup_reason));
        break;
    }

    // --- Helligkeit und Encoder ---
    serialPrint("Brightness set to: " + String(appConfig.brightnessMode));
    setBrightnessLevel(appConfig.brightnessMode);
    initRotaryEncoder(appConfig.brightnessMode, myRotaryEncoderCallback);

    // --- Systemzeit wiederherstellen ---
    struct tm timeinfo;
    if(getLocalTime(&timeinfo, 0)) {
        serialPrint("Current system time: " + String(asctime(&timeinfo)));
    } else {
        // Noch keine NTP-Synchronisation → gespeicherte Zeit aus NVS laden
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

// Hauptschleife — läuft dauerhaft nach setup()
void loop() {
    // --- Hardware-Eingaben und Verbindungen ---
    ledUpdate();
    wifiLoop();
    rotary_loop();

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

// Callback: wird aufgerufen wenn der Webserver Einstellungen ändert
void onStateUpdatedFromWifi(StateChangeType type, void* data) {
    switch(type) {
    case STATE_CHANGE_CONFIG: {
        // Neue Lampen-Konfiguration übernehmen und sofort anwenden
        appConfig = *static_cast<const FullConfig*>(data);
        checkAndApplyColorMode(appConfig);
        setBrightnessLevel(appConfig.brightnessMode);
        setAlarms(appConfig.alarms);
        saveFullConfig(appConfig, true);
        logEvent("Config updated via WiFi");
        serialPrint("Full configuration updated via WiFi controller generic callback.");
        break;
    }
    case STATE_CHANGE_SYSTEM_CONFIG: {
        // Netzwerkeinstellungen geändert → WiFi neu starten damit SSID/PW wirksam wird
        const SystemSettings* newSettings = static_cast<const SystemSettings*>(data);
        systemSettings = *newSettings;
        saveSystemSettings(systemSettings);
        startAlert(ALERT_OK, 2000);
        stopWifi();
        startWifi();
        logEvent("System settings updated via WiFi");
        serialPrint("System settings updated via WiFi controller generic callback.");
        break;
    }
    case STATE_CHANGE_WARNINGS: {
        systemSettings.warningsEnabled = *static_cast<bool*>(data);
        saveSystemSettings(systemSettings);
        logEvent(systemSettings.warningsEnabled ? "Warnings enabled" : "Warnings disabled");
        serialPrint(String("Warnings ") + (systemSettings.warningsEnabled ? "enabled" : "disabled"));
        break;
    }
    case STATE_CHANGE_DEEP_SLEEP: {
        systemSettings.deepSleepEnabled = *static_cast<bool*>(data);
        saveSystemSettings(systemSettings);
        logEvent(systemSettings.deepSleepEnabled ? "Deep sleep enabled" : "Deep sleep disabled");
        serialPrint(String("Deep sleep ") + (systemSettings.deepSleepEnabled ? "enabled" : "disabled"));
        break;
    }
    }
}
