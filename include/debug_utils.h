#ifndef DEBUG_UTILS_H
#define DEBUG_UTILS_H
// Debug-Ausgaben: gibt Meldungen auf die serielle Schnittstelle aus wenn DEBUG definiert ist.

#include <Arduino.h>

// Gibt text auf Serial aus — kompiliert zu No-op wenn DEBUG nicht definiert ist
void serialPrint(const String& text);

// Startet die serielle Ausgabe (einmal am Anfang von setup() aufrufen)
void debugInit();

#endif // DEBUG_UTILS_H
