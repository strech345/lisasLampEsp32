// LED-Steuerung: WS2812FX-Wrapper für RGBW-LEDs (Anzahl einstellbar) mit Helligkeitsstufen,
// Flash-Effekten und Status-LED.
#include <Arduino.h>
#include "types.h"

void ledInit(uint8_t ledCount);     // ledCount = Anzahl der LEDs (SystemSettings::ledCount)
void setLedCount(uint8_t count);    // Anzahl sofort ändern (1–MAX_LED_COUNT)
void ledUpdate();           // Muss in loop() aufgerufen werden
void setLedColor(uint32_t color);                             // eine Farbe für alle Stufen
void setLedColors(uint32_t baseColor, uint32_t boostColor);    // Normal- und Boost-Farbe (Stufe 7)
void setAnimationMode(uint8_t mode);
void setAnimationSpeed(uint16_t speed);
void setBrightnessLevel(uint8_t level);
void startCelebrationEffect();
void startApWaitingEffect();   // blauer Komet: Hotspot wartet auf ein Gerät
void statusLedInit();
void statusLedOn();
void statusLedOff();
void statusLedTick(uint8_t blinkCount); // 0 = langsam blinken, 1–3 = so oft kurz, Pause (aus loop())

// Blendet kurz alle LEDs in color ein und stellt danach den vorherigen Zustand wieder her
void flashLeds(uint32_t color, unsigned long durationMs = 200);
