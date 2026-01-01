#ifndef RGB_EFFECTS_H
#define RGB_EFFECTS_H

#include "types.h"

/**
 * @brief Calculates a color for a sunrise effect based on progress.
 * @param progress The progress of the sunrise, from 0.0 (start) to 1.0 (end).
 * @return The calculated RGB color.
 */
RGB sunriseFade(float progress);

#endif // RGB_EFFECTS_H
