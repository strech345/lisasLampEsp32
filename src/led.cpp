
#include <Arduino.h>
#include <WS2812FX.h>
#include "debug_utils.h"
#include "led.h"

// Structure to hold LED state
struct LedState {
    uint32_t color;
    uint8_t brightness;
    uint8_t mode;
};

static LedState savedLedState;

#define LED_PIN 7
#define LED_COUNT 5
#define STATUS_LED_PIN 8

WS2812FX ws2812fx = WS2812FX(LED_COUNT, LED_PIN, NEO_GRBW + NEO_KHZ800);

void ledInit() {
    statusLedInit();
    ws2812fx.init();
    // ws2812fx.setBrightness(100);
    ws2812fx.setSpeed(200);
    // ws2812fx.setMode(FX_MODE_BREATH);
    ws2812fx.start();
    ws2812fx.setColor(0xFFC896);
}

void ledUpdate() {
    ws2812fx.service();
}

void setBrightness(uint8_t brightness) {
    Serial.println("Setting brightness to " + String(brightness));
    ws2812fx.setBrightness(brightness);
}

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    uint32_t color = (r << 16) | (g << 8) | b;
    const uint32_t current = ws2812fx.getColor();
    if(color == current)
        return; // No change needed
    setLedColor(color);
}

void setLedColor(uint32_t color) {
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

void setAnimationSpeed(uint16_t speed) {
    Serial.println("Setting animation speed to " + String(speed));
    ws2812fx.setSpeed(speed);
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
    if(level == 0) {
        if(ws2812fx.isRunning()) {
            Serial.println("Stopping WS2812FX (brightness 0)");
            ws2812fx.stop();
        }
    } else {
        if(!ws2812fx.isRunning()) {
            Serial.println("Starting WS2812FX (brightness > 0)");
            ws2812fx.start();
        }
    }
    static const uint8_t brightnessMap[8] = {0, 32, 64, 96, 128, 160, 200, 255};
    if(level > 7)
        level = 7;
    Serial.println("Setting brightness level to " + String(level) + " (mapped to " + String(brightnessMap[level])
                   + ")");
    setBrightness(brightnessMap[level]);
}

void startCelebrationEffect() {
    ws2812fx.setMode(FX_MODE_RAINBOW);
    ws2812fx.setSpeed(1000);
    setBrightnessLevel(7);
}

void statusLedInit() {
    pinMode(STATUS_LED_PIN, OUTPUT);
}

void statusLedOn() {
    digitalWrite(STATUS_LED_PIN, LOW);
}

void statusLedOff() {
    digitalWrite(STATUS_LED_PIN, HIGH);
}
