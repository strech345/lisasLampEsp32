// Debug-Ausgaben — Implementierung
#include "debug_utils.h"
#include <Arduino.h>

// DEBUG kommt aus den build_flags in platformio.ini (-DDEBUG)

void serialPrint(const String& text) {
#ifdef DEBUG
    Serial.println(text);
#endif
}