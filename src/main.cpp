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
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "lamp_state.h"
#include "sleep_manager.h"

// --- Globale Zustandsvariablen ---
static std::vector<Route> apRoutes;
static uint32_t s_pendingFlash = 0; // wird von Button-Handlern gesetzt, in updateLed() angewendet

static FullConfig appConfig;
static SystemSettings systemSettings;
static LampState lampState = LAMP_STATE_DEFAULT;
static WiFiTestTracker wifiTracker;
static LampState lastNormalState = LAMP_STATE_DEFAULT;

// --- Auftragsablage Web -> loop() (K7) ---
// Der Callback läuft im async_tcp-Task, loop() im Arduino-Task. Pro Auftragstyp gibt es einen
// Puffer + Flag, geschützt durch einen Mutex (letzter Auftrag je Typ gewinnt). Das ist kleiner und
// einfacher als eine Queue mit großen Elementen (FullConfig/SystemSettings).
struct PendingCommands {
    bool hasConfig = false;
    FullConfig config;
    bool hasSystem = false;
    SystemSettings system;
    bool hasWarnings = false, warnings = false;
    bool hasDeepSleep = false, deepSleep = false;
    bool hasTsf = false, tsf = false;
    bool hasRds = false, rds = false;
    bool hasRtc = false, rtc = false;
    bool ntpResync = false;
    bool tsfSnapshot = false;
    bool restart = false;
    bool factoryReset = false;
};
static PendingCommands s_pending;
static StaticSemaphore_t s_pendingMutexBuf;
static SemaphoreHandle_t s_pendingMutex = xSemaphoreCreateMutexStatic(&s_pendingMutexBuf);

// Verzögerte Aktionen (damit die HTTP-Antwort noch rausgeht): 0 = nichts geplant
static unsigned long s_wifiRestartAt = 0;  // WLAN-Neustart nach Netzwerkeinstellungs-Änderung
static unsigned long s_restartAt = 0;      // ESP.restart() (ggf. mit vorherigem Werksreset)
static bool s_factoryResetOnRestart = false;
static const unsigned long DEFERRED_ACTION_DELAY_MS = 500;

void onStateUpdatedFromWifi(StateChangeType type, void* data);
static void processPendingCommands();

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
    initLampState(&lampState, &lastNormalState, &systemSettings.warningsEnabled, &appConfig, &wifiTracker,
                  &s_pendingFlash);
    initSleepManager(&systemSettings.deepSleepEnabled, &lampState, &appConfig);
    initButtonCallbacks(&appConfig, &lampState, &wifiTracker);
    initTsfClock(&systemSettings, &wifiTracker);
    initRdsClock(&systemSettings, &wifiTracker);

    setAlarms(appConfig.alarms); // Alarme wiederherstellen und nächste Auslösezeit berechnen

    // --- WiFi und Webserver starten ---
    apRoutes = initRouteHandlers(&appConfig, &systemSettings, &wifiTracker, onStateUpdatedFromWifi);
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
    processPendingCommands();

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

// Callback aus dem async_tcp-Task: legt den Auftrag nur ab (kopiert die Daten unter Mutex), führt nichts aus.
void onStateUpdatedFromWifi(StateChangeType type, void* data) {
    xSemaphoreTake(s_pendingMutex, portMAX_DELAY);
    switch(type) {
    case STATE_CHANGE_CONFIG:
        s_pending.config = *static_cast<const FullConfig*>(data);
        s_pending.hasConfig = true;
        break;
    case STATE_CHANGE_SYSTEM_CONFIG:
        s_pending.system = *static_cast<const SystemSettings*>(data);
        s_pending.hasSystem = true;
        break;
    case STATE_CHANGE_WARNINGS:
        s_pending.warnings = *static_cast<bool*>(data);
        s_pending.hasWarnings = true;
        break;
    case STATE_CHANGE_DEEP_SLEEP:
        s_pending.deepSleep = *static_cast<bool*>(data);
        s_pending.hasDeepSleep = true;
        break;
    case STATE_CHANGE_TSF_FALLBACK:
        s_pending.tsf = *static_cast<bool*>(data);
        s_pending.hasTsf = true;
        break;
    case STATE_CHANGE_RDS_CLOCK:
        s_pending.rds = *static_cast<bool*>(data);
        s_pending.hasRds = true;
        break;
    case STATE_CHANGE_RTC_CRYSTAL:
        s_pending.rtc = *static_cast<bool*>(data);
        s_pending.hasRtc = true;
        break;
    case STATE_CHANGE_NTP_RESYNC:
        s_pending.ntpResync = true;
        break;
    case STATE_CHANGE_TSF_SNAPSHOT:
        s_pending.tsfSnapshot = true;
        break;
    case STATE_CHANGE_RESTART:
        s_pending.restart = true;
        break;
    case STATE_CHANGE_FACTORY_RESET:
        s_pending.factoryReset = true;
        break;
    }
    xSemaphoreGive(s_pendingMutex);
}

// Führt alle abgelegten Aufträge aus (nur aus loop() aufrufen) und erledigt verzögerte Aktionen
// (WLAN-Neustart, Restart/Werksreset nach ~500 ms, damit die HTTP-Antwort noch rausgeht).
static void processPendingCommands() {
    // --- Aufträge unter Mutex in lokale Kopie übernehmen und Ablage leeren ---
    PendingCommands cmd;
    xSemaphoreTake(s_pendingMutex, portMAX_DELAY);
    bool any = s_pending.hasConfig || s_pending.hasSystem || s_pending.hasWarnings || s_pending.hasDeepSleep ||
               s_pending.hasTsf || s_pending.hasRds || s_pending.hasRtc || s_pending.ntpResync ||
               s_pending.tsfSnapshot || s_pending.restart || s_pending.factoryReset;
    if(any) {
        cmd = s_pending;
        s_pending = PendingCommands();
    }
    xSemaphoreGive(s_pendingMutex);

    if(any) {
        if(cmd.hasConfig) {
            // Neue Lampen-Konfiguration übernehmen; Helligkeit bleibt (Drehknopf ist Master)
            byte currentBrightness = appConfig.brightnessMode;
            appConfig = cmd.config;
            appConfig.brightnessMode = currentBrightness;
            checkAndApplyColorMode(appConfig);
            setAlarms(appConfig.alarms);
            saveFullConfig(appConfig, true);
            logEvent("Config updated via WiFi");
            serialPrint("Full configuration updated via WiFi.");
        }
        if(cmd.hasSystem) {
            // Nur Netzwerk- und Warn-/Sleep-Felder übernehmen (andere Flags gehören anderen Modulen)
            bool networkChanged = strcmp(systemSettings.internalSSID, cmd.system.internalSSID) != 0 ||
                                  strcmp(systemSettings.internalPW, cmd.system.internalPW) != 0 ||
                                  strcmp(systemSettings.externalSSID, cmd.system.externalSSID) != 0 ||
                                  strcmp(systemSettings.externalPW, cmd.system.externalPW) != 0 ||
                                  strcmp(systemSettings.externalSSID2, cmd.system.externalSSID2) != 0 ||
                                  strcmp(systemSettings.externalPW2, cmd.system.externalPW2) != 0;
            memcpy(systemSettings.internalSSID, cmd.system.internalSSID, sizeof(systemSettings.internalSSID));
            memcpy(systemSettings.internalPW, cmd.system.internalPW, sizeof(systemSettings.internalPW));
            memcpy(systemSettings.externalSSID, cmd.system.externalSSID, sizeof(systemSettings.externalSSID));
            memcpy(systemSettings.externalPW, cmd.system.externalPW, sizeof(systemSettings.externalPW));
            memcpy(systemSettings.externalSSID2, cmd.system.externalSSID2, sizeof(systemSettings.externalSSID2));
            memcpy(systemSettings.externalPW2, cmd.system.externalPW2, sizeof(systemSettings.externalPW2));
            systemSettings.warningsEnabled = cmd.system.warningsEnabled;
            systemSettings.deepSleepEnabled = cmd.system.deepSleepEnabled;
            saveSystemSettings(systemSettings);
            startAlert(ALERT_OK, 200);
            if(networkChanged) {
                s_wifiRestartAt = millis() + DEFERRED_ACTION_DELAY_MS; // WLAN verzögert neu starten
                if(s_wifiRestartAt == 0) s_wifiRestartAt = 1;
            }
            logEvent("System settings updated via WiFi");
            serialPrint("System settings updated via WiFi.");
        }
        // Reine Flag-Änderungen: nur Flag setzen + speichern, kein WLAN-Neustart, kein Alert
        if(cmd.hasWarnings) {
            systemSettings.warningsEnabled = cmd.warnings;
            saveSystemSettings(systemSettings);
            logEvent(cmd.warnings ? "Warnings enabled" : "Warnings disabled");
        }
        if(cmd.hasDeepSleep) {
            systemSettings.deepSleepEnabled = cmd.deepSleep;
            saveSystemSettings(systemSettings);
            logEvent(cmd.deepSleep ? "Deep sleep enabled" : "Deep sleep disabled");
        }
        if(cmd.hasTsf) {
            systemSettings.tsfFallbackEnabled = cmd.tsf;
            saveSystemSettings(systemSettings);
            logEvent(cmd.tsf ? "TSF fallback enabled" : "TSF fallback disabled");
        }
        if(cmd.hasRds) {
            systemSettings.rdsClockEnabled = cmd.rds;
            saveSystemSettings(systemSettings);
            logEvent(cmd.rds ? "RDS clock enabled" : "RDS clock disabled");
        }
        if(cmd.hasRtc) {
            // Taktquelle ist nur beim Boot umschaltbar → speichern und verzögert neu starten
            systemSettings.rtcCrystalEnabled = cmd.rtc;
            saveSystemSettings(systemSettings);
            logEvent("RTC: Einstellung geändert, Neustart");
            cmd.restart = true;
        }
        if(cmd.ntpResync) {
            logEvent("NTP sync triggered via web");
            syncTimeWithNTP();
        }
        if(cmd.tsfSnapshot) {
            triggerTsfSnapshot();
        }
        if(cmd.restart || cmd.factoryReset) {
            logEvent(cmd.factoryReset ? "Factory reset via web" : "Restart via web");
            s_factoryResetOnRestart = s_factoryResetOnRestart || cmd.factoryReset;
            s_restartAt = millis() + DEFERRED_ACTION_DELAY_MS;
            if(s_restartAt == 0) s_restartAt = 1;
        }
    }

    // --- Verzögerte Aktionen ---
    if(s_wifiRestartAt != 0 && (long)(millis() - s_wifiRestartAt) >= 0) {
        s_wifiRestartAt = 0;
        stopWifi();
        startWifi();
    }
    if(s_restartAt != 0 && (long)(millis() - s_restartAt) >= 0) {
        if(s_factoryResetOnRestart) {
            resetFullConfig(); // direkt vor dem Restart, ausstehendes Speichern darf nichts überschreiben
            resetSystemSettings();
        } else {
            flushPendingSave(); // ausstehende Config sofort schreiben
        }
        ESP.restart();
    }
}
