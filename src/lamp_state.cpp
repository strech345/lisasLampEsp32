// Lampenzustands-Maschine — Implementierung
// checkLampState() prüft alle aktiven Modi und setzt den globalen LampState;
// updateLed() erkennt Zustandswechsel und löst die passenden LED-Übergänge aus.
#include "lamp_state.h"
#include <Arduino.h>
#include "led.h"
#include "alerts.h"
#include "alarm.h"
#include "good_night.h"
#include "store.h"
#include "celebration.h"
#include "debug_utils.h"
#include "event_log.h"
#include "wifi_controller.h"

// Farben für Hinweise, Warnungen und Tasten-Aufblitzen (0x00RRGGBB)
#define COLOR_GREEN 0x0000FF00
#define COLOR_RED 0x00FF0000
#define COLOR_YELLOW 0x00FFFF00

// Zeiger auf den globalen Zustand — werden in initLampState() gesetzt
static LampState* s_lampState = nullptr;
static bool* s_warningsEnabled = nullptr;
static FullConfig* s_appConfig = nullptr;
static WiFiTestTracker* s_wifiTracker = nullptr;

// Interner Zustand des Moduls
static LampState s_stateBeforeAlert = LAMP_STATE_DEFAULT; // wird nach einem Hinweis wiederhergestellt
static bool s_apAnimationDismissed = false; // Nutzer hat während der Hotspot-Animation bedient
static uint32_t s_pendingFlash = 0; // Farbe für kurzes Aufblitzen (Tasten-Feedback), 0 = keins

// Initialisiert das Modul mit Zeigern auf die globalen Variablen aus main.cpp
void initLampState(LampState* pLampState, bool* pWarningsEnabled, FullConfig* pAppConfig,
                   WiFiTestTracker* pWifiTracker) {
    s_lampState = pLampState;
    s_warningsEnabled = pWarningsEnabled;
    s_appConfig = pAppConfig;
    s_wifiTracker = pWifiTracker;
}

// Setzt die LED-Farbe passend zum konfigurierten Farbmodus (Weiß-Töne oder Custom-RGB)
void checkAndApplyColorMode(const FullConfig& config) {
    // Aktives Szene (Index abgesichert)
    const LightSet& set = config.sets[config.activeSet < config.setCount ? config.activeSet : 0];
    RGB colorToApply;
    String modeName;

    // Weißtöne für RGBW-LEDs, Format 0xWWRRGGBB (WW = zusätzliche weiße LED).
    // Die weiße LED dominiert stark; deutliche Unterschiede entstehen nur, wenn sie für warme/kalte
    // Töne zurückgenommen und Farbe zugemischt wird (statt nur etwas Farbe obendrauf).
    // boostHex: Farbe auf Stufe 7 — weiße LED voll bzw. Farb-Chips dazu für deutlich mehr Licht;
    // der Ton verschiebt sich dabei etwas (am Gerät abgestimmt)
    uint32_t colorHex = 0;
    uint32_t boostHex = 0;
    switch(set.colorMode) {
    case 0: // Kaltweiß (~6000 K): weiße LED etwas reduziert, Blau dazu
        colorHex = 0xC00000A0;
        boostHex = 0xFF6080FF;
        modeName = "Cool White";
        break;
    case 1: // Neutralweiß (~4000 K): nur die weiße LED
        colorHex = 0xFF000000;
        boostHex = 0xFFFFB060;
        modeName = "Neutral White";
        break;
    case 2: // Warmweiß (~2700 K): weiße LED stark reduziert, kräftig Bernstein (Rot + etwas Grün)
        colorHex = 0x50FF6000;
        boostHex = 0xA0FF8000;
        modeName = "Warm White";
        break;
    case 4: // Kerzenlicht (~2200 K): weiße LED fast aus, Rot mit wenig Grün
        colorHex = 0x18FF3800;
        boostHex = 0x40FF5000;
        modeName = "Candle";
        break;
    case 3: // Benutzerdefinierte Farbe (ohne Boost)
        colorToApply = set.color;
        colorHex = (uint32_t(0) << 24) | (uint32_t(colorToApply.r) << 16) | (uint32_t(colorToApply.g) << 8)
            | uint32_t(colorToApply.b);
        boostHex = colorHex;
        modeName = "Custom";
        break;
    default:
        colorHex = 0xFF000000;
        boostHex = colorHex;
        modeName = "Neutral White (default)";
        break;
    }

    setLedColors(colorHex, boostHex);
    setAnimationMode(set.animationMode);
    setAnimationSpeed(config.animationSpeed);
    serialPrint("LED color set to " + modeName + " (Hex: " + String(colorHex, HEX) + ")");
}

// Prüft ob der Zustand ein Kurzzeit-Alert-Zustand ist
static bool isAlertState(LampState s) {
    return s == LAMP_STATE_ERROR || s == LAMP_STATE_WARNING || s == LAMP_STATE_SUCCESS;
}

// Überlagernde Zustände: legen sich vorübergehend über den eigentlichen Zustand, der danach
// wiederhergestellt wird (kurze Hinweise und die Hotspot-Warte-Animation)
static bool isOverlayState(LampState s) {
    return isAlertState(s) || s == LAMP_STATE_AP_WAITING;
}

// Bestimmt den aktuellen Lampenzustand anhand der aktiven Modi — Priorität von oben nach unten:
// Alert > Hotspot wartet > Alarm > Good Night > Celebration > Sleep > Systemwarnung > Default
void checkLampState() {
    // --- Kurzzeit-Alerts (höchste Priorität) ---
    LampState alertState = LAMP_STATE_DEFAULT;
    switch(getActiveAlertType()) {
    case ALERT_ERROR:
        alertState = LAMP_STATE_ERROR;
        break;
    case ALERT_WARNING:
        alertState = LAMP_STATE_WARNING;
        break;
    case ALERT_OK:
        alertState = LAMP_STATE_SUCCESS;
        break;
    default:
        break;
    }
    // --- Hotspot wartet auf ein Gerät: Vorrang auch vor dem Wecker (per Bedienung ausgelöst);
    //     ein laufender Wecker läuft im Hintergrund weiter und erscheint danach wieder ---
    // Bedienung hat Vorrang: nach Drücken/Drehen bleibt die Animation für diesen Hotspot-Durchgang aus
    // (zurückgesetzt sobald der Hotspot nicht mehr wartet — Gerät verbunden oder Hotspot aus)
    bool apWaiting = isApWaitingForClient();
    if(!apWaiting)
        s_apAnimationDismissed = false;
    LampState overlay = alertState;
    if(overlay == LAMP_STATE_DEFAULT && apWaiting && !s_apAnimationDismissed) {
        overlay = LAMP_STATE_AP_WAITING;
    }
    if(overlay != LAMP_STATE_DEFAULT) {
        // Beim Beginn einer Überlagerung den Zustand davor merken (nicht bei Überlagerung → Überlagerung)
        if(!isOverlayState(*s_lampState)) {
            s_stateBeforeAlert = *s_lampState;
        }
        *s_lampState = overlay;
        return;
    }
    if(isOverlayState(*s_lampState)) {
        // Überlagerung beendet → Zustand davor wiederherstellen und normal weiter prüfen;
        // updateLed() setzt die LEDs beim Zustandswechsel passend neu
        *s_lampState = s_stateBeforeAlert;
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

    // --- Dauerhafte Systemwarnungen (nur wenn Warnungen aktiviert sind und die Lampe an ist;
    //     eine ausgeschaltete Lampe (Helligkeit 0) wird nie durch Warnungen eingeschaltet) ---
    if(*s_warningsEnabled && s_appConfig->brightnessMode != 0) {
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
    case LAMP_STATE_AP_WAITING:  return "AP waiting";
    default:                     return "Unknown";
    }
}

// Helligkeit für kurze Hinweise: Nutzer-Helligkeit, bei ausgeschalteter Lampe volle Helligkeit,
// damit der Hinweis sichtbar ist
static uint8_t alertBrightness() {
    return s_appConfig->brightnessMode ? s_appConfig->brightnessMode : 7;
}

// Setzt die LEDs beim Eintritt in einen neuen Lampenzustand (einmalig pro Zustandswechsel)
static void applyStateEntry(LampState state) {
    switch(state) {
    case LAMP_STATE_DEFAULT:
        setBrightnessLevel(s_appConfig->brightnessMode);
        checkAndApplyColorMode(*s_appConfig);
        break;
    case LAMP_STATE_CELEBRATION:
        startCelebrationEffect();
        break;
    case LAMP_STATE_SUCCESS:
        setBrightnessLevel(alertBrightness());
        setLedColor(COLOR_GREEN);
        setAnimationMode(0); // statisch
        break;
    case LAMP_STATE_ERROR:
        setBrightnessLevel(alertBrightness());
        setLedColor(COLOR_RED);
        setAnimationMode(0); // statisch
        break;
    case LAMP_STATE_WARNING:
        setBrightnessLevel(alertBrightness());
        setLedColor(COLOR_YELLOW);
        setAnimationMode(0); // statisch
        break;
    case LAMP_STATE_SYS_ERROR:
        // Nutzer-Helligkeit: Systemwarnungen treten nur bei eingeschalteter Lampe auf
        setBrightnessLevel(s_appConfig->brightnessMode);
        setLedColor(COLOR_RED);
        setAnimationMode(0); // statisch
        break;
    case LAMP_STATE_SYS_WARNING:
        setBrightnessLevel(s_appConfig->brightnessMode);
        setLedColor(COLOR_YELLOW);
        setAnimationMode(0); // statisch
        break;
    case LAMP_STATE_ALARM:
        checkAndApplyColorMode(*s_appConfig);
        setBrightnessLevel(getAlarmBrightness(s_appConfig->alarmDuration)); // sofort, nicht erst nach 5 s
        break;
    case LAMP_STATE_GOOD_NIGHT:
        // Farbe neu setzen (z. B. nach einem Alert) und Helligkeit sofort anwenden
        checkAndApplyColorMode(*s_appConfig);
        setBrightnessLevel(getGoodNightBrightness(s_appConfig->brightnessMode, s_appConfig->goodNightDuration));
        break;
    case LAMP_STATE_SLEEP:
        setBrightnessLevel(0); // Lampe ausschalten
        break;
    case LAMP_STATE_AP_WAITING:
        startApWaitingEffect();
        break;
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
    if(*s_lampState != lastLampState) {
        applyStateEntry(*s_lampState);
    }
    lastLampState = *s_lampState;

    // --- Kurz-Aufblitzen (z.B. Button-Feedback) ---
    if(s_pendingFlash) {
        flashLeds(s_pendingFlash);
        s_pendingFlash = 0;
    }

    // --- Periodische Helligkeitsanpassung alle 5 Sekunden ---
    if(currentTime - lastCheck < 5000) {
        return;
    }
    lastCheck = currentTime;

    if(*s_lampState == LAMP_STATE_ALARM) {
        setBrightnessLevel(getAlarmBrightness(s_appConfig->alarmDuration));
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        setBrightnessLevel(getGoodNightBrightness(s_appConfig->brightnessMode, s_appConfig->goodNightDuration));
    }
}

// Bedienung (Drücken/Drehen) während der Hotspot-Animation bricht den Hotspot ab: Animation beenden,
// Hotspot ausschalten (Heimnetz bleibt) und den eigentlichen Zustand wiederherstellen.
// Rückgabe: true wenn abgebrochen wurde (der Aufrufer entscheidet, ob die Bedienung weiterwirkt).
bool dismissApAnimation() {
    if(*s_lampState != LAMP_STATE_AP_WAITING)
        return false;
    s_apAnimationDismissed = true;
    stopAccessPoint();
    *s_lampState = s_stateBeforeAlert;
    return true;
}

// Wechselt das Szene um delta Schritte (zyklisch) — Drehen bei gedrückter Taste.
// Ist die Lampe aus (Sleep oder Helligkeit 0), geht sie auf Stufe 1 an; Gute Nacht, Sleep und
// Systemwarnung wechseln in den Normalbetrieb. Ein wartender Hotspot wird abgebrochen.
void switchLightSet(int delta) {
    FullConfig& cfg = *s_appConfig;
    if(cfg.setCount == 0 || delta == 0)
        return;
    dismissApAnimation();
    int n = cfg.setCount;
    cfg.activeSet = (uint8_t)((((int)cfg.activeSet + delta) % n + n) % n);

    bool wasOff = *s_lampState == LAMP_STATE_SLEEP || cfg.brightnessMode == 0;
    if(wasOff)
        cfg.brightnessMode = 1;
    switch(*s_lampState) {
    case LAMP_STATE_GOOD_NIGHT:
        stopGoodNightMode();
        *s_lampState = LAMP_STATE_DEFAULT;
        break;
    case LAMP_STATE_SLEEP:
    case LAMP_STATE_SYS_WARNING:
    case LAMP_STATE_SYS_ERROR:
        *s_lampState = LAMP_STATE_DEFAULT;
        break;
    default:
        break;
    }
    // Sofort anzeigen: im Normalbetrieb Set + Helligkeit, beim Wecker nur das Set
    if(*s_lampState == LAMP_STATE_DEFAULT) {
        checkAndApplyColorMode(cfg);
        setBrightnessLevel(cfg.brightnessMode);
    } else if(*s_lampState == LAMP_STATE_ALARM) {
        checkAndApplyColorMode(cfg);
    }
    saveFullConfig(cfg, true);
    char buf[24];
    snprintf(buf, sizeof(buf), "Scene %u", cfg.activeSet + 1);
    logEvent(buf);
}

// Reagiert auf kurzen Drucktaster-Druck: je nach Zustand stoppen oder Gute-Nacht starten
void onShortPress() {
    // Druck während der Hotspot-Animation: nur abbrechen, keine weitere Aktion (kein Blitz)
    if(dismissApAnimation())
        return;
    if(getActiveAlertType() != ALERT_NONE) {
        serialPrint("Clearing alert via button");
        stopAlert();
        // Zustand von vor dem Alert wiederherstellen (falls der Alert-Zustand schon aktiv war);
        // updateLed() setzt die LEDs beim Zustandswechsel passend neu
        if(isAlertState(*s_lampState)) {
            *s_lampState = s_stateBeforeAlert;
        }
        return;
    }
    if(*s_lampState == LAMP_STATE_SLEEP) {
        serialPrint("stopping sleep mode");
        *s_lampState = LAMP_STATE_DEFAULT;
        s_pendingFlash = COLOR_GREEN;
        return;
    }
    if(*s_lampState == LAMP_STATE_ALARM) {
        serialPrint("stop alarm");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopActiveAlarm();
        s_pendingFlash = COLOR_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
        serialPrint("stop good night");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopGoodNightMode();
        s_pendingFlash = COLOR_RED;
        return;
    }
    if(*s_lampState == LAMP_STATE_CELEBRATION) {
        serialPrint("stop celebration");
        *s_lampState = LAMP_STATE_DEFAULT;
        stopCelebration();
        s_pendingFlash = COLOR_RED;
        return;
    }
    // Normalbetrieb → Gute-Nacht-Modus starten
    serialPrint("activate good night");
    activateGoodNightMode();
    s_pendingFlash = COLOR_GREEN;
}
