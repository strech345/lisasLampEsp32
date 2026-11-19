// LED-Steuerung: WS2812FX-Wrapper für 5 RGBW-LEDs mit Helligkeitsstufen,
// Flash-Effekten und Status-LED.
#include <Arduino.h>

void ledInit();
void ledUpdate();           // Muss in loop() aufgerufen werden
void setLedColor(uint32_t color);
void setAnimationMode(uint8_t mode);
void setAnimationSpeed(uint16_t speed);
void setBrightnessLevel(uint8_t level);
void startCelebrationEffect();
void statusLedInit();
void statusLedOn();
void statusLedOff();
void statusLedTick(bool wifiActive); // Immer: langsam (kein WLAN) oder schnell (WLAN aktiv)

// Blendet kurz alle LEDs in color ein und stellt danach den vorherigen Zustand wieder her
void flashLeds(uint32_t color, unsigned long durationMs = 200);
