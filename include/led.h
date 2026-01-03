
#include <Arduino.h>

void ledInit();
void ledUpdate();
void setBrightness(uint8_t brightness);
void setLedColor(uint8_t r, uint8_t g, uint8_t b);
void setAnimationMode(uint8_t mode);
void setAnimationSpeed(uint16_t speed);
void nextEffect();
void previousEffect();
const char* getEffectName();
uint8_t getEffectCount();
const char* getEffectName(uint8_t index);
void setBrightnessLevel(uint8_t level);

/**
 * Temporarily set color and brightness for a given time (ms), then restore
 * previous state.
 */
void stay(uint8_t r, uint8_t g, uint8_t b, uint8_t level, unsigned long timeMs);