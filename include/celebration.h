#ifndef CELEBRATION_H
#define CELEBRATION_H

#include <Arduino.h>

/**
 * @brief Periodically check if a celebration should start or stop based on the current date and time.
 */
void checkCelebrationLogic();

/**
 * @brief Manually stop the current celebration.
 */
void stopCelebration();

/**
 * @brief Returns true if celebration mode is currently active.
 */
bool isCelebrationActive();

/**
 * @brief Returns the milliseconds until the next celebration starts.
 */
long getMillisToNextCelebration();

#endif // CELEBRATION_H
