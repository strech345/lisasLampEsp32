#include "store.h"

#include <Preferences.h>

#include "debug_utils.h"

#include "types.h"
#include <Arduino.h>

static Preferences preferences;

// Define a namespace for preferences to avoid conflicts
const char* PREF_NAMESPACE = "lisas_lamp_cfg";
const char* CONFIG_KEY = "fullConfig";

// Debounced save support
static FullConfig pendingConfig;
// static bool savePending = false;
static unsigned long lastSaveRequestTime = 0;
// static unsigned long lastSavedTime = 0;

bool saveFullConfig(const FullConfig& config, bool debounce) {
    if(debounce) {
        pendingConfig = config;
        // savePending = true;
        lastSaveRequestTime = millis();
        return true;
    }
    lastSaveRequestTime = 0; // Reset debounce timer

    if(!preferences.begin(PREF_NAMESPACE,
                          false)) { // Open preferences in R/W mode
        serialPrint("Failed to open preferences for writing");
        return false;
    }

    // Use putBytes to save the entire struct
    size_t bytesWritten = preferences.putBytes(CONFIG_KEY, &config, sizeof(FullConfig));
    preferences.end(); // Close preferences

    if(bytesWritten == sizeof(FullConfig)) {
        serialPrint("FullConfig saved successfully!");
        // lastSavedTime = millis();
        // savePending = false;
        return true;
    } else {
        serialPrint("Failed to save FullConfig. Bytes written: " + String(bytesWritten));
        return false;
    }
}

// Call this regularly (e.g. from loop)
void checkToSave() {
    if(lastSaveRequestTime != 0) {
        if(millis() - lastSaveRequestTime >= 5000) {
            serialPrint("Debounced: Saving FullConfig after 5s");
            saveFullConfig(pendingConfig, false);
            // lastSaveRequestTime = 0; // Reset debounce timer
        }
    }
}

bool loadFullConfig(FullConfig& config) {
    if(!preferences.begin(PREF_NAMESPACE,
                          true)) { // Open preferences in Read-only mode
        serialPrint("Failed to open preferences for reading");
        return false;
    }

    // Use getBytes to load the entire struct
    size_t bytesRead = preferences.getBytes(CONFIG_KEY, &config, sizeof(FullConfig));
    preferences.end(); // Close preferences

    if(bytesRead == sizeof(FullConfig)) {
        serialPrint("FullConfig loaded successfully!");
        return true;
    } else {
        serialPrint("Failed to load FullConfig. Bytes read: " + String(bytesRead));
        return false;
    }
}

FullConfig getDefaultFullConfig() {
    FullConfig defaultConfig;

    // Default RGB color (e.g., white)
    defaultConfig.color = {255, 255, 255};
    defaultConfig.colorMode = 1;      // Default to Neutral White
    defaultConfig.animationMode = 1;  // Default to Breathing
    defaultConfig.brightnessMode = 7; // Default to maximum brightness

    // Default alarms (all inactive)
    for(int i = 0; i < MAX_ALARMS; ++i) {
        defaultConfig.alarms[i] = {0, 0, 0, false}; // day=0 (not set), hour=0, minute=0, active=false
    }
    defaultConfig.goodNightDuration = 30;
    defaultConfig.alarmDuration = 30;
    defaultConfig.animationSpeed = 200;

    return defaultConfig;
}

bool resetFullConfig() {
    serialPrint("Resetting FullConfig to defaults...");

    // 1. Delete existing config
    /* if (!deleteFullConfig()) {
        serialPrint("Failed to delete existing config during reset.");
        //return false;
    } */

    // 2. Get default config
    FullConfig defaultConfig = getDefaultFullConfig();

    // 3. Save default config
    if(!saveFullConfig(defaultConfig, false)) {
        serialPrint("Failed to save default config during reset.");
        return false;
    }

    serialPrint("FullConfig successfully reset to defaults.");

    return true;
}

bool ensureConfigExistsAndResetIfNot() {
    serialPrint("Checking if FullConfig exists...");

    if(!preferences.begin(PREF_NAMESPACE,
                          true)) { // Open preferences in Read-only mode
        serialPrint("Failed to open preferences to check config existence.");
        // If we can't even open preferences, we can't check, so attempt to
        // reset
        serialPrint("Attempting to reset FullConfig due to preferences "
                    "open failure.");
        return resetFullConfig();
    }

    size_t storedSize = preferences.getBytesLength(CONFIG_KEY);
    preferences.end(); // Close preferences

    if(storedSize == 0) {
        serialPrint("No FullConfig found. Resetting to defaults.");
        return resetFullConfig();

    } else if(storedSize != sizeof(FullConfig)) {
        serialPrint("Stored FullConfig size mismatch. Resetting to defaults.");
        return resetFullConfig();
    } else {
        serialPrint("FullConfig exists and is valid.");
        return true;
    }
}

const char* SYSTEM_SETTINGS_KEY = "systemSettings";

SystemSettings getDefaultSystemSettings() {
    SystemSettings defaultSettings;

    // Initialize with default values
    strcpy(defaultSettings.internalSSID, "Lizas-Lamp"); // AP_SSID
    strcpy(defaultSettings.internalPW, "");   // AP_PASSWORD (empty for open network)
    strcpy(defaultSettings.externalSSID,
           "Gastzugang_test"); // STA_SSID
    strcpy(defaultSettings.externalPW,
           "AufstehenIstSchoen43="); // STA_PASSWORD

    return defaultSettings;
}

bool saveSystemSettings(const SystemSettings& settings) {
    if(!preferences.begin(PREF_NAMESPACE, false)) {
        serialPrint("Failed to open preferences for writing");
        return false;
    }

    size_t bytesWritten = preferences.putBytes(SYSTEM_SETTINGS_KEY, &settings, sizeof(SystemSettings));
    preferences.end();

    if(bytesWritten == sizeof(SystemSettings)) {
        serialPrint("SystemSettings saved successfully!");
        return true;
    } else {
        serialPrint("Failed to save SystemSettings. Bytes written: " + String(bytesWritten));
        return false;
    }
}

bool loadSystemSettings(SystemSettings& settings) {
    if(!preferences.begin(PREF_NAMESPACE, true)) {
        serialPrint("Failed to open preferences for reading");
        return false;
    }

    size_t bytesRead = preferences.getBytes(SYSTEM_SETTINGS_KEY, &settings, sizeof(SystemSettings));
    preferences.end();

    if(bytesRead == sizeof(SystemSettings)) {
        serialPrint("SystemSettings loaded successfully!");
        return true;
    } else {
        serialPrint("Failed to load SystemSettings. Bytes read: " + String(bytesRead));
        // If loading fails, initialize with default values
        memset(&settings, 0, sizeof(SystemSettings)); // Zero out the struct
        return false;
    }
}

bool resetSystemSettings() {
    serialPrint("Resetting SystemSettings to defaults...");

    // Get default settings
    SystemSettings defaultSettings = getDefaultSystemSettings();

    // Save default settings
    if(!saveSystemSettings(defaultSettings)) {
        serialPrint("Failed to save default system settings during reset.");
        return false;
    }

    serialPrint("SystemSettings successfully reset to defaults.");
    return true;
}

bool ensureSystemSettingsExistsAndResetIfNot() {
    serialPrint("Checking if SystemSettings exists...");

    if(!preferences.begin(PREF_NAMESPACE, true)) {
        serialPrint("Failed to open preferences to check system settings "
                    "existence.");
        serialPrint("Attempting to reset SystemSettings due to preferences "
                    "open failure.");
        return resetSystemSettings();
    }

    size_t storedSize = preferences.getBytesLength(SYSTEM_SETTINGS_KEY);
    preferences.end();

    if(storedSize == 0) {
        serialPrint("No SystemSettings found. Resetting to defaults.");
        return resetSystemSettings();
    } else if(storedSize != sizeof(SystemSettings)) {
        serialPrint("Stored SystemSettings size mismatch. Resetting to defaults.");
        return resetSystemSettings();
    } else {
        serialPrint("SystemSettings exists and is valid.");
        return true;
    }
}
