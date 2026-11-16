// Drehencoder- und Tastersteuerung — Implementierung
#include "button.h"

#include <functional>

#include "AiEsp32RotaryEncoder.h"
#include "Arduino.h"
#include "alerts.h"
#include "lamp_state.h"
#include "led.h"
#include "store.h"
#include "event_log.h"
#include "debug_utils.h"
#include "wifi_controller.h"

static FullConfig* s_appConfig = nullptr;
static LampState* s_lampState = nullptr;
static WiFiTestTracker* s_wifiTracker = nullptr;

// Speichert Zeiger auf den globalen Zustand für die Callback-Funktionen
void initButtonCallbacks(FullConfig* pAppConfig, LampState* pLampState, WiFiTestTracker* pWifiTracker) {
    s_appConfig = pAppConfig;
    s_lampState = pLampState;
    s_wifiTracker = pWifiTracker;
}

// Callback für alle Encoder-Ereignisse: Drehen, kurzer Druck, langer Druck und Boot-Button
void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value) {
    switch(eventType) {
    case RotaryEncoderEventType::ShortClick:
        serialPrint("ShortClick Event!");
        onShortPress();
        break;
    case RotaryEncoderEventType::LongClick:
        // Langer Druck: WiFi einschalten mit gelbem Warnsignal als visuelles Feedback
        serialPrint("LongClick Event!");
        logEvent("WiFi started (long press)");
        startAlert(ALERT_WARNING, 5000);
        startWifi();
        break;
    case RotaryEncoderEventType::Rotate: {
        // Helligkeit ändern (0–7), sofort speichern und anwenden
        int v = value;
        if(v < 0) v = 0;
        if(v > 7) v = 7;
        s_appConfig->brightnessMode = (byte)v;
        serialPrint("Brightness mode set to: " + String(s_appConfig->brightnessMode) + String(" (from ")
                    + String(value) + String(")"));
        saveFullConfig(*s_appConfig, true);
        setBrightnessLevel(s_appConfig->brightnessMode);
        // Drehen beendet Good-Night-Modus (Nutzer will aktiv die Helligkeit steuern)
        if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
            *s_lampState = LAMP_STATE_DEFAULT;
        }
        break;
    }
    case RotaryEncoderEventType::ShortBootClick:
        // Reserviert — aktuell ohne Funktion
        serialPrint("ShortBootClick Event!");
        break;
    case RotaryEncoderEventType::LongBootClick:
        // Langer Boot-Druck: Werkszustand wiederherstellen und neu starten
        serialPrint("LongBootClick Event!");
        logEvent("Factory reset triggered");
        serialPrint("Resetting configuration to defaults...");
        resetFullConfig();
        resetSystemSettings();
        serialPrint("Configuration reset. Restarting ESP...");
        delay(1000);
        ESP.restart();
        break;
    }
}

static RotaryEncoderCallback _rotaryEncoderCallback;

// Pin-Belegung des KY-040 Encoders
#define ROTARY_ENCODER_CLK_PIN 1
#define ROTARY_ENCODER_DT_PIN  2
#define ROTARY_ENCODER_SW_PIN  3
#define ROTARY_ENCODER_STEPS   4
#define BOOT_BUTTON_PIN        9

// Entprellzeiten in Millisekunden
const unsigned long shortPressAfterMiliseconds = 50;   // Mindestdauer für kurzen Druck
const unsigned long longPressAfterMiliseconds  = 1000; // Mindestdauer für langen Druck

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_DT_PIN, ROTARY_ENCODER_CLK_PIN,
                                                          ROTARY_ENCODER_SW_PIN, -1, ROTARY_ENCODER_STEPS);

// Interrupt-Handler: wird vom Encoder-Chip aufgerufen bei jeder Pegelwechsel
void IRAM_ATTR readEncoderISR() {
    rotaryEncoder.readEncoder_ISR();
}

// Initialisiert Encoder-Pins, Grenzen und Startwert
void initRotaryEncoder(byte value, RotaryEncoderCallback callback) {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    _rotaryEncoderCallback = callback;
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    bool circleValues = false;
    serialPrint("Initializing rotary encoder with value: " + String(value));
    rotaryEncoder.setBoundaries(0, 7, circleValues);
    rotaryEncoder.setEncoderValue(value);
}

// Zustand für die Drucktaster-Erkennung
struct LampButtonState {
    unsigned long lastTimeButtonDown = 0;
    bool wasButtonDown = false;
    bool longPressHandled = false;
};

// Allgemeine Drucktaster-Logik: erkennt kurzen und langen Druck, verhindert Doppelauslösung
void process_button(bool isDown, LampButtonState& state, RotaryEncoderEventType shortEvent,
                    RotaryEncoderEventType longEvent, unsigned long currentMillis) {
    if(isDown) {
        if(!state.wasButtonDown) {
            // Taste frisch gedrückt → Zeitstempel merken
            state.lastTimeButtonDown = currentMillis;
            state.longPressHandled = false;
        } else if(!state.longPressHandled && (currentMillis - state.lastTimeButtonDown >= longPressAfterMiliseconds)) {
            // Langer Druck erkannt → einmal auslösen
            if(_rotaryEncoderCallback) {
                _rotaryEncoderCallback(longEvent, 0);
            }
            state.longPressHandled = true;
        }
        state.wasButtonDown = true;
        return;
    }

    // Taste losgelassen: kurzer Druck wenn kein langer Druck stattfand
    if(state.wasButtonDown && !state.longPressHandled) {
        if(currentMillis - state.lastTimeButtonDown >= shortPressAfterMiliseconds) {
            if(_rotaryEncoderCallback) {
                _rotaryEncoderCallback(shortEvent, 0);
            }
        }
    }
    state.wasButtonDown = false;
}

void handle_rotary_button(unsigned long currentMillis) {
    static LampButtonState rotaryState;
    process_button(rotaryEncoder.isEncoderButtonDown(), rotaryState, RotaryEncoderEventType::ShortClick,
                   RotaryEncoderEventType::LongClick, currentMillis);
}

void handle_boot_button(unsigned long currentMillis) {
    static LampButtonState bootState;
    process_button(digitalRead(BOOT_BUTTON_PIN) == LOW, bootState, RotaryEncoderEventType::ShortBootClick,
                   RotaryEncoderEventType::LongBootClick, currentMillis);
}

// Prüft ob der Encoder gedreht wurde und löst den Callback aus
void handleRotation() {
    int16_t encoderDelta = rotaryEncoder.encoderChanged();
    if(encoderDelta != 0 && _rotaryEncoderCallback) {
        int16_t encoderValue = rotaryEncoder.readEncoder();
        _rotaryEncoderCallback(RotaryEncoderEventType::Rotate, encoderValue);
    }
}

// Läuft in der Hauptschleife: prüft alle 20ms Taster und Drehbewegung
void rotary_loop() {
    unsigned long currentMillis = millis();
    static unsigned long previousMillis = 0;
    const unsigned long interval = 20;

    if(!_rotaryEncoderCallback) {
        return;
    }

    if(currentMillis - previousMillis >= interval) {
        previousMillis = currentMillis;
        handle_rotary_button(currentMillis);
        handle_boot_button(currentMillis);
        handleRotation();
    }
}
