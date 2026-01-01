
#include <Arduino.h>
#include <WS2812FX.h>
#include "debug_utils.h"

// Structure to hold LED state
struct LedState {
    uint32_t color;
    uint8_t brightness;
    uint8_t mode;
};

static LedState savedLedState;
static bool isStayActive = false;
static unsigned long stayEndMillis = 0;

#define LED_PIN 27
#define LED_COUNT 1

void restoreLedState();

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void ledInit() {
    ws2812fx.init();
    // ws2812fx.setBrightness(100);
    ws2812fx.setSpeed(200);
    // ws2812fx.setMode(FX_MODE_BREATH);
    ws2812fx.start();
    ws2812fx.setColor(0xFFC896);
}

void ledUpdate() {
    ws2812fx.service();
    if(isStayActive && millis() >= stayEndMillis) {
        restoreLedState();
    }
}

void setBrightness(uint8_t brightness) {
    ws2812fx.setBrightness(brightness);
}

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = (r << 16) | (g << 8) | b;
    const uint32_t current = ws2812fx.getColor();
    if(color == current)
        return; // No change needed
    ws2812fx.setColor(color);
}

void setAnimationMode(uint8_t mode) {
    if(mode == 0) {
        ws2812fx.setMode(FX_MODE_STATIC);
    } else if(mode == 1) {
        ws2812fx.setMode(FX_MODE_BREATH);
    } else {
        ws2812fx.setMode(FX_MODE_STATIC); // default
    }
}

void nextEffect() {
    uint8_t mode = (ws2812fx.getMode() + 1) % ws2812fx.getModeCount();
    ws2812fx.setMode(mode);
    Serial.println("Set effect to mode " + String(mode) + ": " + String(ws2812fx.getModeName(mode)));
}

void previousEffect() {
    uint8_t mode = (ws2812fx.getMode() - 1 + ws2812fx.getModeCount()) % ws2812fx.getModeCount();

    ws2812fx.setMode(mode);
    Serial.println("Set effect to mode " + String(mode) + ": " + String(ws2812fx.getModeName(mode)));
}

/**
 * @brief Sets the LED brightness using a mapped value (0-7).
 * @param level Brightness level (0-7)
 */
void setBrightnessLevel(uint8_t level) {
    ws2812fx.getBrightness();
    if(level == 0 && ws2812fx.isRunning()) {
        ws2812fx.stop();
    } else if(!ws2812fx.isRunning()) {
        ws2812fx.start();
    }
    static const uint8_t brightnessMap[8] = {0, 32, 64, 96, 128, 160, 200, 255};
    if(level > 7)
        level = 7;
    Serial.println("Setting brightness level to " + String(level) + " (mapped to " + String(brightnessMap[level])
                   + ")");
    setBrightness(brightnessMap[level]);
}

/* const char* getEffectName() {
    return ws2812fx.getModeName(ws2812fx.getMode());
}

uint8_t getEffectCount() {
    return ws2812fx.getModeCount();
} */
void restoreLedState() {
    ws2812fx.setColor(savedLedState.color);
    ws2812fx.setBrightness(savedLedState.brightness);
    ws2812fx.setMode(savedLedState.mode);
    isStayActive = false;
}

void stay(uint8_t r, uint8_t g, uint8_t b, uint8_t level, unsigned long timeMs) {
    // Save current state
    savedLedState.color = ws2812fx.getColor();
    savedLedState.brightness = ws2812fx.getBrightness();
    savedLedState.mode = ws2812fx.getMode();
    isStayActive = true;
    stayEndMillis = millis() + timeMs;

    // Map level (0-7) to brightness
    static const uint8_t brightnessMap[8] = {0, 32, 64, 96, 128, 160, 200, 255};
    if(level > 7)
        level = 7;
    uint8_t brightness = brightnessMap[level];

    // Apply new style
    ws2812fx.setColor((r << 16) | (g << 8) | b);
    ws2812fx.setBrightness(brightness);
    ws2812fx.setMode(0); // Use mode 0 (usually static) for stay
}