#include "led.h"
#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include "state.h" // Include state.h to access color override variables
#include "debug_utils.h" // For serialPrint

#define PIN 32
#define NUMPIXELS  1
Adafruit_NeoPixel strip(NUMPIXELS, PIN, NEO_RGBW + NEO_KHZ800);

static RGB defaultColors[] = {
    {173, 216, 230},  // Cool White
    {255, 255, 255},  // Neutral White
    {255, 200, 150}   // Warm white
};

unsigned long nextAnimationFrame = 0;

/**
 * @brief Gets the color based on the current color mode.
 * @param modeRanges The array of mode settings.
 * @return The calculated 32-bit color value.
 */
static uint32_t getColor(const RANGE modeRanges[]) {
    int colorMode = modeRanges[1].cur;
    if (colorMode == 3) {
        const RANGE& hue = modeRanges[3];
        const RANGE& sat = modeRanges[4];
        return strip.ColorHSV(map(hue.cur, hue.min, hue.max, 0, 65535), map(sat.cur, sat.min, sat.max, 0, 255), 255);
    } else {
        RGB& defaultColor = defaultColors[colorMode];
        return strip.Color(defaultColor.r, defaultColor.g, defaultColor.b);
    }
}


/**
 * @brief Converts a 32-bit color value to an RGB struct.
 * @param color The 32-bit color value.
 * @return An RGB struct.
 */
static RGB getRGB(uint32_t color) {
    RGB rgb;
    rgb.r = (color >> 16) & 0xFF;
    rgb.g = (color >> 8) & 0xFF;
    rgb.b = color & 0xFF;
    return rgb;
}


/**
 * @brief Initializes the NeoPixel strip.
 */
void ledSetup() {
    strip.begin();
    strip.show();
}


/**
 * @brief Sets the color of the NeoPixel strip based on the current mode or color override.
 * @param modeRanges The array of mode settings.
 */
void ledUpdateColor(const RANGE modeRanges[]) {
    if (colorOverrideActive) {
        serialPrint("Applying color override: R" + String(overrideColor.r) + ", G" + String(overrideColor.g) + ", B" + String(overrideColor.b));
        strip.setPixelColor(0, strip.Color(overrideColor.r, overrideColor.g, overrideColor.b));
    } else {
        const uint32_t color = getColor(modeRanges);
        const RGB rgb = getRGB(color);
        serialPrint("Updating color from mode: R" + String(rgb.r) + "," + String(rgb.g) + "," + String(rgb.b));
        strip.setPixelColor(0, color);
    }
}


/**
 * @brief Sets the brightness of the NeoPixel strip.
 * @param brightnessRange The range settings for brightness.
 */
void ledUpdateBrightness(const RANGE& brightnessRange) {
    strip.setBrightness(map(brightnessRange.cur, brightnessRange.min, brightnessRange.max, 0, 255));
}


/**
 * @brief Sends the updated color data to the NeoPixel strip.
 */
void ledShow() {
    strip.show();
}


/**
 * @brief Blinks the LED to indicate the current mode.
 * @param mode The current mode index.
 * @param modeRanges The array of mode settings.
 */
void ledStartBlinking(int mode, const RANGE modeRanges[]) {
    int count = mode + 1;
    int _delay = 450;
    for (size_t i = count; i > 0; i--) {
        strip.setBrightness(0);
        strip.show();
        delay(_delay / 2);
        ledUpdateBrightness(modeRanges[0]);
        ledUpdateColor(modeRanges);
        strip.show();
        if (i != 1) {
            delay(_delay);
        }
    }
}


/**
 * @brief Applies a fire-like flickering effect to the LED.
 * @param currentMs The current time in milliseconds from millis().
 * @param modeRanges The array of mode settings.
 */
void ledFireEffect(unsigned long currentMs, const RANGE modeRanges[]) {
    int flameMode = modeRanges[2].cur;
    if (flameMode == 0 || currentMs < nextAnimationFrame) {
        return;
    }

    nextAnimationFrame = currentMs + random(10, 113);

    RGB currentColor = getRGB(getColor(modeRanges));
    for (int i = 0; i < strip.numPixels(); i++) {
        int flicker = random(0, 55);
        int r1 = currentColor.r - flicker;
        int g1 = currentColor.g - flicker;
        int b1 = currentColor.b - flicker;
        if (g1 < 0) g1 = 0;
        if (r1 < 0) r1 = 0;
        if (b1 < 0) b1 = 0;
        strip.setPixelColor(i, r1, g1, b1);
    }
    strip.show();
}

/**
 * @brief Provides a pulsing blue light effect for the WiFi mode indicator.
 * @param currentMs The current time in milliseconds from millis().
 */
void ledWifiIndicator(unsigned long currentMs) {
    // Pulsing blue effect using a sine wave
    float brightness = (sin(currentMs / 500.0 * PI) + 1.0) / 2.0; // 0.0 to 1.0
    uint8_t blue_brightness = (uint8_t)(brightness * 255);
    strip.setPixelColor(0, strip.Color(0, 0, blue_brightness, 0)); // Blue color
    strip.show();
}