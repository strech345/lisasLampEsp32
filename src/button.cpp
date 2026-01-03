#include "button.h" // Include the updated button header

#include <functional> // Required for std::function

#include "AiEsp32RotaryEncoder.h"
#include "Arduino.h"

// Static variable to store the rotary encoder event callback
static RotaryEncoderCallback _rotaryEncoderCallback;

#define ROTARY_ENCODER_CLK_PIN 32 // D32
#define ROTARY_ENCODER_DT_PIN 21  // D21
#define ROTARY_ENCODER_SW_PIN 4   // D4
#define ROTARY_ENCODER_STEPS 4
#define BOOT_BUTTON_PIN 0

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
    rotaryEncoder.setup(readEncoderISR);
    bool circleValues = false;
    Serial.print("Initializing rotary encoder with value: " + String(value));
    rotaryEncoder.setBoundaries(0, 7, circleValues);
    rotaryEncoder.setEncoderValue(value); // start in the middle
}

void handle_rotary_button(unsigned long currentMillis) {
    static unsigned long lastTimeButtonDown = 0;
    static bool wasButtonDown = false;

    bool isEncoderButtonDown = rotaryEncoder.isEncoderButtonDown();
    if(isEncoderButtonDown) {
        Serial.print("+"); // REMOVE THIS LINE IF YOU DONT WANT TO SEE
        if(!wasButtonDown) {
            // start measuring
            lastTimeButtonDown = currentMillis;
        }
        // else we wait since button is still down
        wasButtonDown = true;
        return;
    }

    // button is up
    if(wasButtonDown) {
        if(currentMillis - lastTimeButtonDown >= longPressAfterMiliseconds) {
            _rotaryEncoderCallback(RotaryEncoderEventType::LongClick, 0);
        } else if(currentMillis - lastTimeButtonDown >= shortPressAfterMiliseconds) {
            _rotaryEncoderCallback(RotaryEncoderEventType::ShortClick, 0);
        }
    }
    wasButtonDown = false;
}

void handle_boot_button(unsigned long currentMillis) {
    static unsigned long lastTimeButtonDown = 0;
    static bool wasButtonDown = false;

    int isBootButtonDown = digitalRead(BOOT_BUTTON_PIN) == LOW;
    if(isBootButtonDown) {
        Serial.print("+"); // REMOVE THIS LINE IF YOU DONT WANT TO SEE
        if(!wasButtonDown) {
            // start measuring
            lastTimeButtonDown = currentMillis;
        }
        // else we wait since button is still down
        wasButtonDown = true;
        return;
    }

    // button is up
    if(wasButtonDown) {
        if(currentMillis - lastTimeButtonDown >= longPressAfterMiliseconds) {
            _rotaryEncoderCallback(RotaryEncoderEventType::LongBootClick, 0);
        } else if(currentMillis - lastTimeButtonDown >= shortPressAfterMiliseconds) {
            _rotaryEncoderCallback(RotaryEncoderEventType::ShortBootClick, 0);
        }
    }
    wasButtonDown = false;
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