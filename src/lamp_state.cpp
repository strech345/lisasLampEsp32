// Lampenzustands-Maschine — Implementierung
// checkLampState() prüft alle aktiven Modi und setzt den globalen LampState;
// updateLed() erkennt Zustandswechsel und löst die passenden LED-Übergänge aus.
#include "lamp_state.h"
#include <Arduino.h>
#include "led.h"
#include "alerts.h"
#include "alarm.h"
#include "good_night.h"
#include "celebration.h"
#include "debug_utils.h"
#include "event_log.h"

#define FLASH_GREEN 0x0000FF00
#define FLASH_RED   0x00FF0000

// Zeiger auf den globalen Zustand — werden in initLampState() gesetzt
static LampState* s_lampState = nullptr;
static LampState* s_lastNormalState = nullptr;
static bool* s_warningsEnabled = nullptr;
static FullConfig* s_appConfig = nullptr;
static WiFiTestTracker* s_wifiTracker = nullptr;
static uint32_t* s_pendingFlash = nullptr;

// Initialisiert das Modul mit Zeigern auf die globalen Variablen aus main.cpp
void initLampState(LampState* pLampState, LampState* pLastNormalState, bool* pWarningsEnabled,
                   FullConfig* pAppConfig, WiFiTestTracker* pWifiTracker, uint32_t* pPendingFlash) {
    s_lampState = pLampState;
    s_lastNormalState = pLastNormalState;
    s_warningsEnabled = pWarningsEnabled;
    s_appConfig = pAppConfig;
    s_wifiTracker = pWifiTracker;
    s_pendingFlash = pPendingFlash;
}

// Setzt die LED-Farbe passend zum konfigurierten Farbmodus (Weiß-Töne oder Custom-RGB)
void checkAndApplyColorMode(const FullConfig& config) {
    RGB colorToApply;
    String modeName;

    uint32_t colorHex = 0;
    switch(config.colorMode) {
    case 0:                    // Kaltweiß
        colorHex = 0xFF000080;
        modeName = "Cool White";
        break;
    case 1:                    // Neutralweiß
        colorHex = 0xFF503000;
        modeName = "Neutral White";
        break;
    case 2:                    // Warmweiß
        colorHex = 0xFF804000;
        modeName = "Warm White";
        break;
    case 3: // Benutzerdefinierte Farbe
        colorToApply = config.color;
        colorHex = (uint32_t(0) << 24) | (uint32_t(colorToApply.r) << 16) | (uint32_t(colorToApply.g) << 8)
            | uint32_t(colorToApply.b);
        modeName = "Custom";
        break;
    default:
        colorHex = 0xFF000000;
        modeName = "Neutral White (default)";
        break;
    }

    setLedColor(colorHex);
    setAnimationMode(config.animationMode);
    setAnimationSpeed(config.animationSpeed);
    serialPrint("LED color set to " + modeName + " (Hex: " + String(colorHex, HEX) + ")");
}

// Bestimmt den aktuellen Lampenzustand anhand der aktiven Modi — Priorität von oben nach unten:
// Alert > Alarm > Good Night > Celebration > Sleep > Systemwarnung > Default
void checkLampState() {
    // --- Kurzzeit-Alerts (höchste Priorität) ---
    switch(getActiveAlertType()) {
    case ALERT_ERROR:
        *s_lampState = LAMP_STATE_ERROR;
        return;
    case ALERT_WARNING:
        *s_lampState = LAMP_STATE_WARNING;
        return;
    case ALERT_OK:
        *s_lampState = LAMP_STATE_SUCCESS;
        return;
    }

    // --- Alarm ---
    if(isAlarmActive()) {
        *s_lampState = LAMP_STATE_ALARM;
        return;
    }
    if(*s_lampState == LAMP_STATE_ALARM) {
        // Alarm gerade beendet → in Sleep wechseln (Nutzer soll explizit aufwecken)
        serialPrint("change from alarm to sleep mode");
        *s_lampState = LAMP_STATE_SLEEP;
        return;
    }

    // --- Gute-Nacht-Modus ---
    if(isGoodNightModeActive()) {
        *s_lampState = LAMP_STATE_GOOD_NIGHT;
        return;
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("Good night mode ended, returning to default lamp state");
        *s_lampState = LAMP_STATE_SLEEP;
        return;
    }

    // --- Feier-Modus ---
    if(isCelebrationActive()) {
        *s_lampState = LAMP_STATE_CELEBRATION;
        return;
    }

    // --- Sleep bleibt bestehen bis Nutzer aufweckt ---
    if(*s_lampState == LAMP_STATE_SLEEP) {
        return;
    }

    // --- Dauerhafte Systemwarnungen (nur wenn Warnungen aktiviert sind) ---
    if(*s_warningsEnabled) {
        time_t now = time(nullptr);
        if(now < 1577836800L) {
            // Systemzeit vor 2020 → nie synchronisiert (Epoch 0)
            *s_lampState = LAMP_STATE_SYS_ERROR;
            return;
        }
        unsigned long sinceSyncMs;
        if(s_wifiTracker->lastSucceededTestTime == 0) {
            // Noch nie synchronisiert → nach 30 Min. Schonfrist als veraltet markieren
            sinceSyncMs = (millis() > 30UL * 60 * 1000) ? (25UL * 60 * 60 * 1000) : 0;
        } else {
            sinceSyncMs = millis() - s_wifiTracker->lastSucceededTestTime;
        }
        if(sinceSyncMs > 24UL * 60 * 60 * 1000) {
            // Letzter NTP-Sync mehr als 24h her
            *s_lampState = LAMP_STATE_SYS_WARNING;
            return;
        }
    }

    *s_lampState = LAMP_STATE_DEFAULT;
}

// Gibt den Namen eines Lampenzustands als Zeichenkette zurück (für Logging)
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

// Wendet LED-Effekte an wenn sich der Lampenzustand geändert hat; aktualisiert Helligkeit im laufenden Betrieb
void updateLed() {
    static LampState lastLampState;
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // --- Zustandswechsel erkennen und Übergangseffekte auslösen ---
    if(*s_lampState != lastLampState) {
        char buf[52];
        snprintf(buf, sizeof(buf), "State: %s -> %s", lampStateName(lastLampState), lampStateName(*s_lampState));
        logEvent(buf);
    }
    if(*s_lampState == LAMP_STATE_DEFAULT && lastLampState != LAMP_STATE_DEFAULT) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        setBrightnessLevel(s_appConfig->brightnessMode);
        checkAndApplyColorMode(*s_appConfig);
    }
    if(*s_lampState == LAMP_STATE_CELEBRATION && lastLampState != LAMP_STATE_CELEBRATION) {
        startCelebrationEffect();
    }
    if(*s_lampState == LAMP_STATE_ERROR && lastLampState != LAMP_STATE_ERROR) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        setLedColor(0x00FF0000); // rot
    }
    if(*s_lampState == LAMP_STATE_WARNING && lastLampState != LAMP_STATE_WARNING) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        setLedColor(0x00FFFF00); // gelb
    }
    if(*s_lampState == LAMP_STATE_SYS_ERROR && lastLampState != LAMP_STATE_SYS_ERROR) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        startSinglePixelBlink(0x00FF0000); // einzelner Pixel blinkt rot
    }
    if(*s_lampState == LAMP_STATE_SYS_WARNING && lastLampState != LAMP_STATE_SYS_WARNING) {
        setBrightnessLevel(s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7);
        startSinglePixelBlink(0x00FFFF00); // einzelner Pixel blinkt gelb
    }
    if(*s_lampState == LAMP_STATE_ALARM && lastLampState != LAMP_STATE_ALARM) {
        if(lastLampState == LAMP_STATE_SYS_ERROR || lastLampState == LAMP_STATE_SYS_WARNING) {
            stopSinglePixelBlink();
        }
        checkAndApplyColorMode(*s_appConfig);
    }
    if(*s_lampState == LAMP_STATE_SLEEP && lastLampState != LAMP_STATE_SLEEP) {
        setBrightnessLevel(0); // Lampe ausschalten
    }
    lastLampState = *s_lampState;

    // --- Kurz-Aufblitzen (z.B. Button-Feedback) ---
    if(*s_pendingFlash) {
        flashLeds(*s_pendingFlash);
        *s_pendingFlash = 0;
    }

    // --- Periodische Helligkeitsanpassung alle 5 Sekunden ---
    if(currentTime - lastCheck < 5000) {
        return;
    }
    lastCheck = currentTime;
    serialPrint("Lamp state check: " + String(*s_lampState));

    if(*s_lampState == LAMP_STATE_ALARM) {
        setBrightnessLevel(getAlarmBrightness(s_appConfig->alarmDuration));
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        setBrightnessLevel(getGoodNightBrightness(s_appConfig->brightnessMode, s_appConfig->goodNightDuration));
    }
}

// Reagiert auf kurzen Drucktaster-Druck: je nach Zustand stoppen oder Gute-Nacht starten
void onShortPress() {
    if(getActiveAlertType() != ALERT_NONE) {
        serialPrint("Clearing alert via button");
        stopAlert();
        *s_lampState = *s_lastNormalState;
        return;
    }
    if(*s_lampState == LAMP_STATE_SLEEP) {
        serialPrint("stopping sleep mode");
        *s_lampState = LAMP_STATE_DEFAULT;
        *s_pendingFlash = FLASH_GREEN;
        return;
    }
    if(*s_lampState == LAMP_STATE_ALARM) {
        serialPrint("stop alarm");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopActiveAlarm();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("stop good night");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopGoodNightMode();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_CELEBRATION) {
        serialPrint("stop celebration");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopCelebration();
        *s_pendingFlash = FLASH_RED;
        return;
    }
    // Normalbetrieb → Gute-Nacht-Modus starten
    serialPrint("activate good night");
    activateGoodNightMode();
    *s_pendingFlash = FLASH_GREEN;
}
