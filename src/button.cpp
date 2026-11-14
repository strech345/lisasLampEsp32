#include "button.h" // Include the updated button header

#include <functional> // Required for std::function

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

void initButtonCallbacks(FullConfig* pAppConfig, LampState* pLampState, WiFiTestTracker* pWifiTracker) {
    s_appConfig = pAppConfig;
    s_lampState = pLampState;
    s_wifiTracker = pWifiTracker;
}

void myRotaryEncoderCallback(RotaryEncoderEventType eventType, int16_t value) {
    switch(eventType) {
    case RotaryEncoderEventType::ShortClick:
        serialPrint("ShortClick Event!");
        onShortPress();
        break;
    case RotaryEncoderEventType::LongClick:
        serialPrint("LongClick Event!");
        logEvent("WiFi started (long press)");
        startAlert(ALERT_WARNING, 5000);
        startWifi();
        break;
    case RotaryEncoderEventType::Rotate: {
        // Clamp value to 0-7, cast to byte, and save to config
        int v = value;
        if(v < 0)
            v = 0;
        if(v > 7)
            v = 7;
        s_appConfig->brightnessMode = (byte)v;
        serialPrint("Brightness mode set to: " + String(s_appConfig->brightnessMode) + String(" (from ")
                    + String(value) + String(")"));
        saveFullConfig(*s_appConfig, true);
        setBrightnessLevel(s_appConfig->brightnessMode);
        if(*s_lampState == LAMP_STATE_GOOD_NIGHT) {
            *s_lampState = LAMP_STATE_DEFAULT;
        }
        break;
    }
    case RotaryEncoderEventType::ShortBootClick:
        serialPrint("ShortBootClick Event!");
        // startWifi();
        break;
    case RotaryEncoderEventType::LongBootClick:
        serialPrint("LongBootClick Event!");
        logEvent("Factory reset triggered");
        serialPrint("Resetting configuration to defaults...");
        resetFullConfig();
        resetSystemSettings();
        serialPrint("Configuration reset. Restarting ESP...");
        delay(1000); // Give time for serial to flush
        ESP.restart();
        break;
    }
}

// Static variable to store the rotary encoder event callback
static RotaryEncoderCallback _rotaryEncoderCallback;

#define ROTARY_ENCODER_CLK_PIN 1
#define ROTARY_ENCODER_DT_PIN 2
#define ROTARY_ENCODER_SW_PIN 3
#define ROTARY_ENCODER_STEPS 4
#define BOOT_BUTTON_PIN 9

// paramaters for button
unsigned long shortPressAfterMiliseconds = 50;  // how long short press shoud be. Do not set too low to avoid bouncing
                                                // (false press events).
unsigned long longPressAfterMiliseconds = 1000; // how long čong press shoud be.

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
// unsigned long previousMillis = 0;  // will store last time LED was updated

// constants won't change :
// const long interval = 1000;  // interval at which to blink (milliseconds)

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ROTARY_ENCODER_DT_PIN, ROTARY_ENCODER_CLK_PIN,
                                                          ROTARY_ENCODER_SW_PIN, -1, ROTARY_ENCODER_STEPS);

/**
 * @brief Interrupt service routine for the rotary encoder.
 * This is called by the AiEsp32RotaryEncoder library.
 */
void IRAM_ATTR readEncoderISR() {
    rotaryEncoder.readEncoder_ISR();
}

void initRotaryEncoder(byte value, RotaryEncoderCallback callback) {
    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
    _rotaryEncoderCallback = callback;
    rotaryEncoder.begin();
    // pinMode(ROTARY_ENCODER_SW_PIN, INPUT_PULLDOWN);
    rotaryEncoder.setup(readEncoderISR);
    bool circleValues = false;
    Serial.print("Initializing rotary encoder with value: " + String(value));
    rotaryEncoder.setBoundaries(0, 7, circleValues);
    rotaryEncoder.setEncoderValue(value); // start in the middle
}

struct LampButtonState {
    unsigned long lastTimeButtonDown = 0;
    bool wasButtonDown = false;
    bool longPressHandled = false;
};
void process_button(bool isDown, LampButtonState& state, RotaryEncoderEventType shortEvent,
                    RotaryEncoderEventType longEvent, unsigned long currentMillis) {
    // void process_button(bool isDown, LampButtonState& state, RotaryEncoderEventType shortEvent,
    //                    RotaryEncoderEventType longEvent, unsigned long currentMillis) {
    if(isDown) {
        if(!state.wasButtonDown) {
            // start measuring
            state.lastTimeButtonDown = currentMillis;
            state.longPressHandled = false;
        } else if(!state.longPressHandled && (currentMillis - state.lastTimeButtonDown >= longPressAfterMiliseconds)) {
            if(_rotaryEncoderCallback) {
                _rotaryEncoderCallback(longEvent, 0);
            }
            state.longPressHandled = true;
        }
        state.wasButtonDown = true;
        return;
    }

    // button is up
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
    // Debugging logic: Print pin state every 500ms
    /* static unsigned long lastDebug = 0;
    if (currentMillis - lastDebug > 500) {
        lastDebug = currentMillis;
        Serial.print("DEBUG: SW Pin Raw: ");
        Serial.print(digitalRead(ROTARY_ENCODER_SW_PIN));
        Serial.print(" | Lib isDown: ");
        Serial.println(rotaryEncoder.isEncoderButtonDown());
    } */

    // Fix: Treat HIGH as pressed because pin reads LOW (0) when idle.
    // This handles cases like Active High buttons or NC switches to GND.
    // bool kButtonDown = digitalRead(ROTARY_ENCODER_SW_PIN) == HIGH;
    process_button(rotaryEncoder.isEncoderButtonDown(), rotaryState, RotaryEncoderEventType::ShortClick,
                   RotaryEncoderEventType::LongClick, currentMillis);

    // process_button(kButtonDown, rotaryState, RotaryEncoderEventType::ShortClick, RotaryEncoderEventType::LongClick,
    //                currentMillis);
}

void handle_boot_button(unsigned long currentMillis) {
    static LampButtonState bootState;
    process_button(digitalRead(BOOT_BUTTON_PIN) == LOW, bootState, RotaryEncoderEventType::ShortBootClick,
                   RotaryEncoderEventType::LongBootClick, currentMillis);
    // process_button(digitalRead(BOOT_BUTTON_PIN) == LOW, bootState, RotaryEncoderEventType::ShortBootClick,
    //               RotaryEncoderEventType::LongBootClick, currentMillis);
}

void handleRotation() {
    // lets see if anything changed
    int16_t encoderDelta = rotaryEncoder.encoderChanged();

    // for some cases we only want to know if value is increased or decreased
    // (typically for menu items)
    if(encoderDelta != 0 && _rotaryEncoderCallback) { // Check if callback is set
        int16_t encoderValue = rotaryEncoder.readEncoder();
        _rotaryEncoderCallback(RotaryEncoderEventType::Rotate, encoderValue);
    }
}

void rotary_loop() {
    unsigned long currentMillis = millis();
    static unsigned long previousMillis = 0;
    const unsigned long interval = 20; // 20ms interval

    if(!_rotaryEncoderCallback) {
        return;
    }

    if(currentMillis - previousMillis >= interval) {
        // save the last time we ran the loop
        previousMillis = currentMillis;

        handle_rotary_button(currentMillis);
        handle_boot_button(currentMillis);
        handleRotation();
    }
}