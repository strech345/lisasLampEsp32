#ifndef CELEBRATION_H
#define CELEBRATION_H
#include "types.h"
// Feier-Modus: startet täglich um 7 Uhr einen Regenbogen-Effekt an konfigurierten
// Feiertagen (Geburtstag, Jahrestag, Neujahr, Heiligabend).

#include <Arduino.h>

// Prüft ob heute ein Feiertagsdatum ist und startet/stoppt den Feier-Modus entsprechend
void checkCelebrationLogic();

// Beendet den Feier-Modus manuell (z.B. durch langen Tastendruck)
void stopCelebration();

// Gibt true zurück wenn der Feier-Modus aktiv ist
bool isCelebrationActive();

// Gibt ms bis zur nächsten Feier zurück (für Deep-Sleep-Berechnung)
int64_t getMillisToNextCelebration();

// Merkt sich die Systemeinstellungen mit den Party-Tagen (einmal in setup())
void initCelebration(const SystemSettings* settings);

#endif // CELEBRATION_H
