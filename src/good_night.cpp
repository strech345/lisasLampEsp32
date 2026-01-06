
#include <stdint.h>

#include "good_night.h"
#include <Arduino.h>
#include <debug_utils.h>

static bool goodNightModeActive = false;
static unsigned long goodNightStartTimeMs = 0;
// const unsigned long GOOD_NIGHT_DURATION_MS =
//     60 * 1000; // 1 minute in milliseconds
// 30 * 60 * 1000ULL; // 30 minutes in milliseconds

void activateGoodNightMode() {
    goodNightModeActive = true;
    goodNightStartTimeMs = millis();
    serialPrint("Lstart good night");
}

void stopGoodNightMode() {
    goodNightModeActive = false;
    goodNightStartTimeMs = 0;
}

bool isGoodNightModeActive() {
    return goodNightModeActive;
}

/**
 * @brief Gets the interpolated brightness for good night mode.
 * @param startBrightness The starting brightness (0-7)
 * @return Brightness value from startBrightness to 0 (fades out), or 0 if not
 * active.
 */
byte getGoodNightBrightness(byte startBrightness, uint16_t durationMinutes) {
    if(!goodNightModeActive) {
        return 0;
    }
    unsigned long durationMs = (unsigned long)durationMinutes * 60 * 1000;
    unsigned long elapsed = millis() - goodNightStartTimeMs;
    if(elapsed >= durationMs) {
        return 0;
    }
    float progress = (float)elapsed / (float)durationMs;
    byte result = startBrightness - (byte)(progress * startBrightness);
    serialPrint("GoodNight Calc: Start=" + String(startBrightness) + " Elapsed=" + String((unsigned long)elapsed) + " Prog=" + String(progress) + " Res=" + String(result));
    return result;
}

/**
 * @brief Checks if good night mode is active and stops it if duration elapsed.
 */
void checkGoodNightMode(uint16_t durationMinutes) {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // Only check every 1 seconds
    if(currentTime - lastCheck < 10000) {
        return;
    }
    lastCheck = currentTime;

    unsigned long durationMs = (unsigned long)durationMinutes * 60 * 1000;
    if(goodNightModeActive
       && (millis() - goodNightStartTimeMs > durationMs)) {
        stopGoodNightMode();
    }
}