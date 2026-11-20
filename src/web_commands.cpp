// Web-Aufträge: Brücke zwischen Webserver (async_tcp-Task) und loop().
// HTTP-Handler rufen queueWebCommand() auf; das kopiert den Auftrag nur unter einem Mutex.
// processWebCommands() läuft in loop() und führt alles aus (LEDs, NVS, WLAN, Neustart), damit
// diese Dinge nie aus einem fremden Task angefasst werden.
#include "web_commands.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "alarm.h"
#include "alerts.h"
#include "debug_utils.h"
#include "event_log.h"
#include "lamp_state.h"
#include "led.h"
#include "store.h"
#include "tsf_clock.h"
#include "wifi_controller.h"

// Zeiger auf den globalen Zustand aus main.cpp
static FullConfig* s_appConfig = nullptr;
static SystemSettings* s_systemSettings = nullptr;

// --- Auftragsablage ---
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
    bool hasLedCount = false;
    uint8_t ledCount = 0;
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

// Merkt sich die Zeiger auf den globalen Zustand (einmal in setup() aufrufen)
void initWebCommands(FullConfig* appConfig, SystemSettings* systemSettings) {
    s_appConfig = appConfig;
    s_systemSettings = systemSettings;
}

// Callback aus dem async_tcp-Task: legt den Auftrag nur ab (kopiert die Daten unter Mutex), führt nichts aus.
void queueWebCommand(StateChangeType type, void* data) {
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
    case STATE_CHANGE_LED_COUNT:
        s_pending.ledCount = *static_cast<uint8_t*>(data);
        s_pending.hasLedCount = true;
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
void processWebCommands() {
    // --- Aufträge unter Mutex in lokale Kopie übernehmen und Ablage leeren ---
    PendingCommands cmd;
    xSemaphoreTake(s_pendingMutex, portMAX_DELAY);
    bool any = s_pending.hasConfig || s_pending.hasSystem || s_pending.hasWarnings || s_pending.hasDeepSleep ||
               s_pending.hasTsf || s_pending.hasRds || s_pending.hasRtc || s_pending.hasLedCount || s_pending.ntpResync ||
               s_pending.tsfSnapshot || s_pending.restart || s_pending.factoryReset;
    if(any) {
        cmd = s_pending;
        s_pending = PendingCommands();
    }
    xSemaphoreGive(s_pendingMutex);

    if(any) {
        if(cmd.hasConfig) {
            // Neue Lampen-Konfiguration übernehmen; Helligkeit bleibt (Drehknopf ist Master)
            byte currentBrightness = s_appConfig->brightnessMode;
            *s_appConfig = cmd.config;
            s_appConfig->brightnessMode = currentBrightness;
            checkAndApplyColorMode(*s_appConfig);
            setAlarms(s_appConfig->alarms);
            saveFullConfig(*s_appConfig, true);
            logEvent("Config updated via WiFi");
            serialPrint("Full configuration updated via WiFi.");
        }
        if(cmd.hasSystem) {
            // Nur Netzwerk- und Warn-/Sleep-Felder übernehmen (andere Flags gehören anderen Modulen)
            bool networkChanged = strcmp(s_systemSettings->internalSSID, cmd.system.internalSSID) != 0 ||
                                  strcmp(s_systemSettings->internalPW, cmd.system.internalPW) != 0 ||
                                  strcmp(s_systemSettings->externalSSID, cmd.system.externalSSID) != 0 ||
                                  strcmp(s_systemSettings->externalPW, cmd.system.externalPW) != 0 ||
                                  strcmp(s_systemSettings->externalSSID2, cmd.system.externalSSID2) != 0 ||
                                  strcmp(s_systemSettings->externalPW2, cmd.system.externalPW2) != 0;
            memcpy(s_systemSettings->internalSSID, cmd.system.internalSSID, sizeof(s_systemSettings->internalSSID));
            memcpy(s_systemSettings->internalPW, cmd.system.internalPW, sizeof(s_systemSettings->internalPW));
            memcpy(s_systemSettings->externalSSID, cmd.system.externalSSID, sizeof(s_systemSettings->externalSSID));
            memcpy(s_systemSettings->externalPW, cmd.system.externalPW, sizeof(s_systemSettings->externalPW));
            memcpy(s_systemSettings->externalSSID2, cmd.system.externalSSID2, sizeof(s_systemSettings->externalSSID2));
            memcpy(s_systemSettings->externalPW2, cmd.system.externalPW2, sizeof(s_systemSettings->externalPW2));
            s_systemSettings->warningsEnabled = cmd.system.warningsEnabled;
            s_systemSettings->deepSleepEnabled = cmd.system.deepSleepEnabled;
            saveSystemSettings(*s_systemSettings);
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
            s_systemSettings->warningsEnabled = cmd.warnings;
            saveSystemSettings(*s_systemSettings);
            logEvent(cmd.warnings ? "Warnings enabled" : "Warnings disabled");
        }
        if(cmd.hasDeepSleep) {
            s_systemSettings->deepSleepEnabled = cmd.deepSleep;
            saveSystemSettings(*s_systemSettings);
            logEvent(cmd.deepSleep ? "Deep sleep enabled" : "Deep sleep disabled");
        }
        if(cmd.hasTsf) {
            s_systemSettings->tsfFallbackEnabled = cmd.tsf;
            saveSystemSettings(*s_systemSettings);
            logEvent(cmd.tsf ? "TSF fallback enabled" : "TSF fallback disabled");
        }
        if(cmd.hasRds) {
            s_systemSettings->rdsClockEnabled = cmd.rds;
            saveSystemSettings(*s_systemSettings);
            logEvent(cmd.rds ? "RDS clock enabled" : "RDS clock disabled");
        }
        if(cmd.hasLedCount) {
            // LED-Anzahl sofort anwenden und speichern
            setLedCount(cmd.ledCount);
            s_systemSettings->ledCount = cmd.ledCount;
            saveSystemSettings(*s_systemSettings);
            char buf[40];
            snprintf(buf, sizeof(buf), "LED count: %u", cmd.ledCount);
            logEvent(buf);
        }
        if(cmd.hasRtc) {
            // Taktquelle ist nur beim Boot umschaltbar → speichern und verzögert neu starten
            s_systemSettings->rtcCrystalEnabled = cmd.rtc;
            saveSystemSettings(*s_systemSettings);
            logEvent("RTC: setting changed, restarting");
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
