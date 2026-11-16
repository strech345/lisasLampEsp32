// LED-Steuerung: WS2812FX-Wrapper für 5 RGBW-LEDs mit Helligkeitsstufen,
// Flash-Effekten, Einzelpixel-Blinken und Status-LED.
#include <Arduino.h>

// Pixel 0 wird für Systemwarnungen (Blinken) verwendet — bleibt fix damit er nicht mit Effekten kollidiert
#define WARNING_PIXEL 0

void ledInit();
void ledUpdate();           // Muss in loop() aufgerufen werden
void setLedColor(uint32_t color);
void setAnimationMode(uint8_t mode);
void setAnimationSpeed(uint16_t speed);
void setBrightnessLevel(uint8_t level); // Setzt Helligkeitsstufe (1–10) und speichert sie intern
void startCelebrationEffect();
void statusLedInit();
void statusLedOn();
void statusLedOff();
void statusLedTick(bool wifiActive); // Immer: langsam (kein WLAN) oder schnell (WLAN aktiv)

// Lässt WARNING_PIXEL in blinkColor blinken; alle anderen Pixel bleiben statisch
void startSinglePixelBlink(uint32_t blinkColor);
void stopSinglePixelBlink();

// Blendet kurz alle LEDs in color ein und stellt danach den vorherigen Zustand wieder her
// color format: 0x00RRGGBB (same as setLedColor)
void flashLeds(uint32_t color, unsigned long durationMs = 350);
