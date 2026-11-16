// Debug-Ausgaben — Implementierung
#include "debug_utils.h"
#include <Arduino.h>

#define DEBUG

void serialPrint(const String& text) {
#ifdef DEBUG
    Serial.println(text);
#endif
}