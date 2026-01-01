#include "state.h"
#include <Arduino.h>
#include <Preferences.h>
#include "led.h" // for led_start_blinking
#include "debug_utils.h" // For serialPrint

#define DEBUG

/* int mode = 0; */
unsigned long lastInteractionMs = 0;
volatile bool encoderChanged = false;
volatile bool buttonChanged = false;

int stateMode = 0; // 0 = normal, 1 = wifi
unsigned long stateModeStartTime = 0;

RGB overrideColor = {255, 255, 255};
bool colorOverrideActive = false;

// EEPROM addresses for new settings
#define EEPROM_ADDR_OVERRIDE_R 6
#define EEPROM_ADDR_OVERRIDE_G 7
#define EEPROM_ADDR_OVERRIDE_B 8
#define EEPROM_ADDR_OVERRIDE_ACTIVE 9

RANGE modeRanges[]{
    {0, 15, 15, 0, 1},  // 0 brightnes
    {0, 3, 0, 0, 2},    // 1 colors -> 0 (def),1 (def),2 (def),3 (custom)
    {0, 1, 0, 0, 3},    // 2 flame
    {0, 63, 63, 0, 4},  // 3 custom hue (active when color = 3)
    {0, 63, 63, 0, 5},  // 4 custom sat (active when color = 3)
};
int NUMBER_OF_MODES = sizeof(modeRanges) / sizeof(RANGE);

#include "preferences_utils.h"




/**
 * @brief Saves the current state of all modes using the Preferences library.
 */
void saveState() {
    Preferences preferences;
    preferences.begin("state", false);
    for (size_t i = 0; i < (size_t)NUMBER_OF_MODES; i++) {
        char key[12];
        sprintf(key, "mode_cur_%d", i);
        putValue(preferences, key, modeRanges[i].cur);
    }
    putValue(preferences, "override_r", overrideColor.r);
    putValue(preferences, "override_g", overrideColor.g);
    putValue(preferences, "override_b", overrideColor.b);
    putValue(preferences, "override_active", colorOverrideActive);
    preferences.end();
    serialPrint("State saved.");
}


/**
 * @brief Restores the state of all modes using the Preferences library.
 */
void restoreState() {
    Preferences preferences;
    preferences.begin("state", true); // read-only
    for (size_t i = 0; i < (size_t)NUMBER_OF_MODES; i++) {
        char key[12];
        sprintf(key, "mode_cur_%d", i);
        modeRanges[i].cur = getValue(preferences, key, modeRanges[i].def);
    }
    overrideColor.r = getValue(preferences, "override_r", 255);
    overrideColor.g = getValue(preferences, "override_g", 255);
    overrideColor.b = getValue(preferences, "override_b", 255);
    colorOverrideActive = getValue(preferences, "override_active", false);
    preferences.end();
}


/**
 * @brief Sets the current operating mode.
 * @param _mode The new mode to set.
 */
void setMode(int _mode) {
    if (_mode >= NUMBER_OF_MODES) {
        _mode = 0;
    } else if ((_mode == 3 || _mode == 4) && modeRanges[1].cur != 3) {
        // custom color not active
        _mode = 0;
    }
    if (mode == _mode) {
        return;
    }
    //mode = _mode;

    /* rotaryEncoder.setBoundaries(modeRanges[mode].min, modeRanges[mode].max, false);
    rotaryEncoder.setEncoderValue(modeRanges[mode].cur);

    serialPrint("mode " + String(mode + 1));
    ledStartBlinking(mode, modeRanges); */
}


/**
 * @brief Validates a value to ensure it is within the allowed range for a given mode.
 * @param mode The mode to validate against.
 * @param value The value to validate.
 * @param def The default value to return if validation fails.
 * @return The validated value or the default value.
 */
int validateValue(int mode, int value, int def) {
    const RANGE& modeRange = modeRanges[mode];
    return (value >= modeRange.min && value <= modeRange.max) ? value : def;
}

/**
 * @brief Sets the RGB values for the color override.
 * @param r Red component (0-255).
 * @param g Green component (0-255).
 * @param b Blue component (0-255).
 */
void setColorOverride(const RGB& color) {
    overrideColor.r = color.r;
    overrideColor.g = color.g;
    overrideColor.b = color.b;
    serialPrint("Color override set to: R" + String(color.r) + " G" + String(color.g) + " B" + String(color.b));
}

/**
 * @brief Sets whether the color override is active.
 * @param active True to activate override, false to deactivate.
 */
void setColorOverrideActive(bool active) {
    colorOverrideActive = active;
    serialPrint("Color override active: " + String(active ? "true" : "false"));
}
