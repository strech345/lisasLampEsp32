
#include <Arduino.h>

// The single pixel used for system warning/error blink (0 = first pixel)
#define WARNING_PIXEL 0

void ledInit();
void ledUpdate();
void setBrightness(uint8_t brightness);
void setLedColor(uint8_t r, uint8_t g, uint8_t b);
void setLedColor(uint32_t color);
void setAnimationMode(uint8_t mode);
void setAnimationSpeed(uint16_t speed);
void nextEffect();
void previousEffect();
const char* getEffectName();
uint8_t getEffectCount();
const char* getEffectName(uint8_t index);
void setBrightnessLevel(uint8_t level);
void startCelebrationEffect();
void statusLedInit();
void statusLedOn();
void statusLedOff();

// Single-pixel blink: WARNING_PIXEL blinks in blinkColor, rest stays static
void startSinglePixelBlink(uint32_t blinkColor);
void stopSinglePixelBlink();

// Briefly flashes all LEDs to color, then restores previous state.
// color format: 0x00RRGGBB (same as setLedColor)
void flashLeds(uint32_t color, unsigned long durationMs = 350);