// Einstiegspunkt der Firmware: initialisiert alle Module in setup() und ruft
// in loop() alle periodischen Prozesse auf (LED, WiFi, Alarm, Deep Sleep u.a.).
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
#include "wifi_controller.h"
#include <system_utils.h>
#include <esp_sleep.h>
#include "alerts.h"
#include "celebration.h"
#include "event_log.h"
#include "tsf_clock.h"
#include "rds_clock.h"
#include "rtc_crystal.h"
#include <soc/rtc.h>
#include "lamp_state.h"
#include "sleep_manager.h"
#include "web_commands.h"

// --- Globale Zustandsvariablen ---
static std::vector<Route> apRoutes;

static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;


// Einmalige Initialisierung beim Start
void setup() {
#ifdef DEBUG
    Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
    Serial.setTxTimeoutMs(0); // nie blockieren wenn kein USB-Host verbunden
#endif
#if ARDUINO_USB_CDC_ON_BOOT
    // Nur bei USB-CDC: warten bis der Host bereit ist (max. 2 Sekunden); bei UART sinnlos
    unsigned long startWait = millis();
    while(!Serial && millis() - startWait < 2000) {
        delay(10);
    }
    if(Serial)
        delay(500); // kurze Pause nur wenn Monitor wirklich verbunden
#endif

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

    // --- Aufwachgrund nach Deep-Sleep auswerten (früh: davon hängen Licht und Hotspot ab) ---
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    bool timerWakeup = false;
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
        timerWakeup = true;
        serialPrint("Wakeup caused by timer (Alarm/WiFi)");
        break;
    default:
        serialPrint("Wakeup was not caused by deep sleep: " + String(wakeup_reason));
        break;
    }

    // --- LED initialisieren und sofort den richtigen Zustand zeigen ---
    // Das Licht kommt VOR WLAN-Init (Kanal-Scan, AP-Start dauern Sekunden), damit die Lampe beim
    // Aufwecken per Taster sofort leuchtet. Timer-Wakeup: LEDs bleiben aus (kein Aufblitzen).
    ledInit();
    checkAndApplyColorMode(appConfig);
    setBrightnessLevel(timerWakeup ? 0 : appConfig.brightnessMode);
    serialPrint("Licht gesetzt nach " + String(millis()) + " ms");

    // --- Systemeinstellungen laden ---
    ensureSystemSettingsExistsAndResetIfNot();
    if(!loadSystemSettings(systemSettings)) {
        serialPrint("Using default system settings");
        systemSettings = getDefaultSystemSettings();
    }

    // --- RTC-Quarz vor allem anderen initialisieren (Taktquelle nur beim Boot umschaltbar) ---
    initRtcCrystal(&systemSettings);

    // --- Module mit Zeigern auf globalen Zustand initialisieren ---
    initLampState(&lampState, &systemSettings.warningsEnabled, &appConfig, &wifiTracker);
    initSleepManager(&systemSettings.deepSleepEnabled, &lampState, &appConfig);
    initButtonCallbacks(&appConfig, &lampState, &wifiTracker);
    initTsfClock(&systemSettings, &wifiTracker);
    initRdsClock(&systemSettings, &wifiTracker);

    setAlarms(appConfig.alarms); // Alarme wiederherstellen und nächste Auslösezeit berechnen

    // --- Webserver-Aufträge und WiFi starten ---
    initWebCommands(&appConfig, &systemSettings);
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, queueWebCommand);
    initWiFiController(systemSettings, apRoutes, wifiTracker);
    startWifi(!timerWakeup); // Timer-Wakeup: stiller NTP-Sync ohne sichtbaren Hotspot

    // --- Encoder (Helligkeit wurde schon direkt nach ledInit() gesetzt) ---
    initRotaryEncoder(appConfig.brightnessMode, myRotaryEncoderCallback);

    // --- Systemzeit wiederherstellen ---
    struct tm timeinfo;
    if(getLocalTime(&timeinfo, 0)) {
        serialPrint("Current system time: " + String(asctime(&timeinfo)));
    } else {
        // Noch keine NTP-Synchronisation → gespeicherte Zeit aus NVS laden
        time_t saved = loadLastKnownTime();
        if(saved > 0) {
            struct timeval tv = {.tv_sec = saved, .tv_usec = 0};
            settimeofday(&tv, nullptr);
            serialPrint("Restored last known time from NVS: " + String((unsigned long)saved));
        } else {
            serialPrint("Current system time: Not synchronized yet, no saved time");
        }
    }
    logEvent("Boot");
    serialPrint("Initialized nach " + String(millis()) + " ms");
}

// Hauptschleife — läuft dauerhaft nach setup()
void loop() {
    // --- Aufträge vom Webserver abarbeiten (läuft hier im Arduino-Task, nicht im async_tcp-Task) ---
    processWebCommands();

    // --- Hardware-Eingaben und Verbindungen ---
    wifiLoop();
    tsfClockLoop();
    rdsClockLoop();
    statusLedTick(isWiFiActive());
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
