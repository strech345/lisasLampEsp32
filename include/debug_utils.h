#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H

#include <Arduino.h> // For String

/**
 * @brief Prints a message to the serial port if DEBUG is enabled.
 * @param text The text to print.
 */
void serialPrint(const String& text);

#endif // DEBUG_UTILS_H
