// Drehencoder- und Tastersteuerung — Implementierung
#include "button.h"
#include "pins.h"

#include <functional>

#include "AiEsp32RotaryEncoder.h"
#include "Arduino.h"
#include "alerts.h"
#include "lamp_state.h"
#include "led.h"
#include "good_night.h"
#include "store.h"
#include "event_log.h"
#include "debug_utils.h"
#include "wifi_controller.h"

static FullConfig* s_appConfig = nullptr;
static LampState* s_lampState = nullptr;
static WiFiTestTracker* s_wifiTracker = nullptr;

// Flanken pro Rastung des KY-040 (voller Quadratur-Zyklus). Die Bibliothek zählt mit Schrittweite 1
// (Rohflanken); das Umrechnen auf Rastungen passiert in handleRotation().
#define ROTARY_ENCODER_STEPS 4

const unsigned long shortPressAfterMiliseconds = 50;
const unsigned long longPressAfterMiliseconds = 1000;

static RotaryEncoderCallback _rotaryEncoderCallback;

// Schrittweite 1 = Rohflanken; letzter Parameter false = interne Pullups statt Pulldowns
// (das KY-040-Modul hat eigene Pullups, interne Pulldowns würden dagegen arbeiten)
AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(PIN_ENCODER_DT, PIN_ENCODER_CLK,
                                                          PIN_ENCODER_SW, -1, 1, false);

// Rastungs-Erkennung: Ruhepegel der beiden Encoder-Pins (beim Start gelernt) und Rohzählerstand
// der zuletzt ausgewerteten Rastung
static int s_restDt = HIGH;
static int s_restClk = HIGH;
static long s_lastDetentPos = 0;

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
        startAlert(ALERT_WARNING, 200);
        startWifi();
        break;
    case RotaryEncoderEventType::Rotate: {
        // Helligkeit ändern (0–7) mit relativer Steuerung, sofort speichern und anwenden
        int currentBrightness = s_appConfig->brightnessMode;
        int newBrightness = currentBrightness + value;
        serialPrint("Rotary encoder rotated by " + String(value) + ", changing brightness from "
                    + String(currentBrightness) + " to " + String(newBrightness));

        // Begrenzungen intern prüfen (0-7)
        if(newBrightness < 0)
            newBrightness = 0;
        if(newBrightness > 7)
            newBrightness = 7;

        if(newBrightness == currentBrightness)
            break; // Keine Änderung

        s_appConfig->brightnessMode = (byte)newBrightness;
        serialPrint("Brightness mode changed by " + String(value) + " to: " + String(newBrightness));
        setBrightnessLevel(s_appConfig->brightnessMode); // setzt Helligkeit und zeigt sofort an
        saveFullConfig(*s_appConfig, true);
        // Drehen heißt "Lampe benutzen": Gute-Nacht wirklich beenden, Sleep/Systemwarnung → Default,
        // damit Zustand und LEDs konsistent sind. Alarm und Alerts bleiben unverändert.
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

// Interrupt-Handler: wird vom Encoder-Chip aufgerufen bei jeder Pegelwechsel
void IRAM_ATTR readEncoderISR() {
    rotaryEncoder.readEncoder_ISR();
}

// Initialisiert Encoder-Pins
void initRotaryEncoder(byte value, RotaryEncoderCallback callback) {
    pinMode(PIN_BOOT_BUTTON, INPUT_PULLUP);
    _rotaryEncoderCallback = callback;
    rotaryEncoder.begin();
    rotaryEncoder.setup(readEncoderISR);
    // Beschleunigung aus: sie addiert bei schnellem Drehen Extra-Flanken, danach passt die Zählung
    // nicht mehr zu den Rastungen und einzelne Rastungen gehen verloren
    rotaryEncoder.setAcceleration(0);
    pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
    pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
    delay(2); // Pullups einschwingen lassen
    // Ruhepegel lernen: beim Start steht der Knopf in einer Rastung
    s_restDt = digitalRead(PIN_ENCODER_DT);
    s_restClk = digitalRead(PIN_ENCODER_CLK);
    if(s_restDt != s_restClk) { // unplausibel (gerade zwischen zwei Rastungen) → KY-040-Standard
        s_restDt = HIGH;
        s_restClk = HIGH;
    }
    s_lastDetentPos = rotaryEncoder.readEncoder();
    serialPrint("Encoder: Ruhepegel DT=" + String(s_restDt) + " CLK=" + String(s_restClk));
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
    process_button(digitalRead(PIN_BOOT_BUTTON) == LOW, bootState, RotaryEncoderEventType::ShortBootClick,
                   RotaryEncoderEventType::LongBootClick, currentMillis);
}

// Wertet Drehungen rastungsgenau aus: Erst wenn der Knopf wieder in einer Rastung steht (beide Pins
// auf Ruhepegel), wird die seit der letzten Rastung gezählte Flankenzahl auf ganze Rastungen gerundet
// und gemeldet. Danach wird der Bezugspunkt neu gesetzt → verlorene oder doppelte Flanken (Prellen)
// verschieben die Zählung nicht dauerhaft, und keine Rastung wird verschluckt.
void handleRotation() {
    if(!_rotaryEncoderCallback)
        return;
    if(digitalRead(PIN_ENCODER_DT) != s_restDt || digitalRead(PIN_ENCODER_CLK) != s_restClk)
        return; // Knopf steht zwischen zwei Rastungen → abwarten
    long pos = rotaryEncoder.readEncoder();
    long delta = pos - s_lastDetentPos;
    s_lastDetentPos = pos;
    // |delta| < 2: nur Prellen in der Rastung → ignorieren
    if(delta > -2 && delta < 2)
        return;
    // Auf ganze Rastungen runden; mindestens eine (bei 2–3 Flanken fehlten Flanken, gedreht wurde trotzdem)
    long detents = (delta + (delta > 0 ? ROTARY_ENCODER_STEPS / 2 : -ROTARY_ENCODER_STEPS / 2)) / ROTARY_ENCODER_STEPS;
    if(detents == 0)
        detents = delta > 0 ? 1 : -1;
    if(detents > 7) detents = 7;   // Wertebereich der Helligkeit, schützt vor int16-Überlauf
    if(detents < -7) detents = -7;
    _rotaryEncoderCallback(RotaryEncoderEventType::Rotate, (int16_t)detents);
}

// Läuft in der Hauptschleife: Drehbewegung sofort, Taster alle 20ms (Debounce)
void rotary_loop() {
    if(!_rotaryEncoderCallback) {
        return;
    }

    // Drehbewegung braucht kein Debounce — sofort prüfen für minimale Latenz
    handleRotation();

    // Taster alle 20ms prüfen (Debounce gegen Prellen)
    unsigned long currentMillis = millis();
    static unsigned long previousMillis = 0;
    if(currentMillis - previousMillis >= 20) {
        previousMillis = currentMillis;
        handle_rotary_button(currentMillis);
        handle_boot_button(currentMillis);
    }
}
