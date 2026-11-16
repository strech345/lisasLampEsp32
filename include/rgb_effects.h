#ifndef RGB_EFFECTS_H
#define RGB_EFFECTS_H
// RGB-Farbeffekte: berechnet Farbverläufe für Animationen (z.B. Sonnenaufgang beim Wecker).

#include "types.h"

// Berechnet die Farbe für einen Sonnenaufgangs-Effekt anhand des Fortschritts (0.0–1.0)
RGB sunriseFade(float progress);

#endif // RGB_EFFECTS_H
