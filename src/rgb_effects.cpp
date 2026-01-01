#include "rgb_effects.h"
#include <Arduino.h>

// Helper for linear interpolation between two values.
byte lerp(byte a, byte b, float t) {
    return a + (b - a) * t;
}

/**
 * @brief Calculates a color for a sunrise effect based on progress.
 * The effect is a two-phase fade from black, to orange, to a bright warm white.
 * @param progress The progress of the sunrise, from 0.0 (start) to 1.0 (end).
 * @return The calculated RGB color.
 */
RGB sunriseFade(float progress) {
    progress = constrain(progress, 0.0, 1.0);

    RGB color;

    if (progress < 0.5) {
        // Phase 1: Fade from Black to Orange (progress 0.0 to 0.5)
        float phase_progress = progress * 2.0; // Scale this phase's progress to a 0.0-1.0 range
        color.r = lerp(0, 255, phase_progress);
        color.g = lerp(0, 120, phase_progress);
        color.b = 0;
    } else {
        // Phase 2: Fade from Orange to Bright Yellow-White (progress 0.5 to 1.0)
        float phase_progress = (progress - 0.5) * 2.0; // Scale this phase's progress to a 0.0-1.0 range
        color.r = 255;
        color.g = lerp(120, 255, phase_progress);
        color.b = lerp(0, 200, phase_progress);
    }

    return color;
}
