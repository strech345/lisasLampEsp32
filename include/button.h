#ifndef BUTTON_H
#define BUTTON_H

#include <functional>
#include <Arduino.h>

// Define an enum for rotary encoder event types
enum class RotaryEncoderEventType {
    ShortClick,
    LongClick,
    Rotate,
    ShortBootClick,
    LongBootClick,
};

// Define the callback function type
using RotaryEncoderCallback =
    std::function<void(RotaryEncoderEventType, int16_t value)>;

/**
 * @brief Initializes the rotary encoder and sets up its interrupt service
 * routine.
 * @param callback A callback function to be called when a rotary encoder event
 * occurs.
 */
void initRotaryEncoder(byte value, RotaryEncoderCallback callback);
void rotary_loop();

#endif // BUTTON_H