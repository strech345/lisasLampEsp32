
#include <stdint.h>

#include "good_night.h"
#include <Arduino.h>
#include <debug_utils.h>

static bool goodNightModeActive = false;
static unsigned long long goodNightStartTimeMicros = 0;
const unsigned long long GOOD_NIGHT_DURATION_MICROS =
    60 * 1000; // 1 minute in microseconds
// 30 * 60 * 1000000ULL; // 30 minutes in microseconds

void activateGoodNightMode() {
    goodNightModeActive = true;
    goodNightStartTimeMicros = millis();
    serialPrint("Lstart good night");
}

void stopGoodNightMode() {
    goodNightModeActive = false;
    goodNightStartTimeMicros = 0;
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
byte getGoodNightBrightness(byte startBrightness) {
    if(!goodNightModeActive) {
        return 0;
    }
    unsigned long long elapsed = millis() - goodNightStartTimeMicros;
    if(elapsed >= GOOD_NIGHT_DURATION_MICROS) {
        return 0;
    }
    float progress = (float)elapsed / (float)GOOD_NIGHT_DURATION_MICROS;
    byte result = startBrightness - (byte)(progress * startBrightness);
    serialPrint("GoodNight Calc: Start=" + String(startBrightness) + " Elapsed=" + String((unsigned long)elapsed) + " Prog=" + String(progress) + " Res=" + String(result));
    return result;
}

/**
 * @brief Checks if good night mode is active and stops it if duration elapsed.
 */
void checkGoodNightMode() {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // Only check every 1 seconds
    if(currentTime - lastCheck < 10000) {
        return;
    }
    lastCheck = currentTime;

    if(goodNightModeActive
       && (millis() - goodNightStartTimeMicros > GOOD_NIGHT_DURATION_MICROS)) {
        stopGoodNightMode();
    }
}