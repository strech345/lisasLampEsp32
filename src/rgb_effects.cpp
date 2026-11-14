#include "rgb_effects.h"
#include <Arduino.h>

// Lineare Interpolation zwischen zwei Bytewerten
byte lerp(byte a, byte b, float t) {
    return a + (b - a) * t;
}

// Berechnet die Farbe für einen Sonnenaufgangs-Effekt anhand des Fortschritts (0.0–1.0)
RGB sunriseFade(float progress) {
    progress = constrain(progress, 0.0, 1.0);

    RGB color;

    // --- Zweiphasiger Farbverlauf ---
    if (progress < 0.5) {
        // Phase 1 (0–50%): Schwarz → Orange
        float phase_progress = progress * 2.0; // Phase auf 0.0–1.0 skalieren
        color.r = lerp(0, 255, phase_progress);
        color.g = lerp(0, 120, phase_progress);
        color.b = 0;
    } else {
        // Phase 2 (50–100%): Orange → helles Warmweiß
        float phase_progress = (progress - 0.5) * 2.0;
        color.r = 255;
        color.g = lerp(120, 255, phase_progress);
        color.b = lerp(0, 200, phase_progress);
    }

    return color;
}
