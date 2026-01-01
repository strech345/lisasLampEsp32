#include "debug_utils.h"
#include <Arduino.h> // For Serial.println

#define DEBUG // Keep this define here for debug_utils.cpp

/**
 * @brief Prints a message to the serial port if DEBUG is enabled.
 * @param text The text to print.
 */
void serialPrint(const String& text) {
#ifdef DEBUG
    Serial.println(text);
#endif
}