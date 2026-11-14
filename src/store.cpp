#include "store.h"

#include <Preferences.h>

#include "debug_utils.h"

#include "types.h"
#include <Arduino.h>

static Preferences preferences;

// Define a namespace for preferences to avoid conflicts
const char* PREF_NAMESPACE = "lisas_lamp_cfg";
const char* CONFIG_KEY = "fullConfig";

template<typename T>
static bool saveStruct(const char* key, const T& data) {
    if(!preferences.begin(PREF_NAMESPACE, false)) {
        serialPrint("Failed to open preferences for writing");
        return false;
    }
    size_t written = preferences.putBytes(key, &data, sizeof(T));
    preferences.end();
    if(written == sizeof(T)) {
        serialPrint(String(key) + " saved successfully");
        return true;
    }
    serialPrint(String(key) + " save failed, bytes written: " + String(written));
    return false;
}

static FullConfig pendingConfig;
static unsigned long lastSaveRequestTime = 0;

bool saveFullConfig(const FullConfig& config, bool debounce) {
    if(debounce) {
        pendingConfig = config;
        // savePending = true;
        lastSaveRequestTime = millis();
        return true;
    }
    lastSaveRequestTime = 0; // Reset debounce timer
    return saveStruct(CONFIG_KEY, config);
}

// Call this regularly (e.g. from loop)
void checkToSave() {
    if(lastSaveRequestTime != 0) {
        if(millis() - lastSaveRequestTime >= 5000) {
            serialPrint("Debounced: Saving FullConfig after 5s");
            saveFullConfig(pendingConfig, false);
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

static bool ensureStructExists(const char* key, size_t expectedSize, bool (*resetFn)()) {
    if(!preferences.begin(PREF_NAMESPACE, true)) {
        preferences.end();
        return resetFn();
    }
    size_t storedSize = preferences.getBytesLength(key);
    preferences.end();
    if(storedSize == 0) {
        return resetFn();
    } else if(storedSize != expectedSize) {
        return resetFn();
    }
    return true;
}

bool ensureConfigExistsAndResetIfNot() {
    serialPrint("Checking if FullConfig exists...");
    return ensureStructExists(CONFIG_KEY, sizeof(FullConfig), resetFullConfig);
}

const char* SYSTEM_SETTINGS_KEY = "systemSettings";

SystemSettings getDefaultSystemSettings() {
    SystemSettings defaultSettings;

    // Initialize with default values
    strcpy(defaultSettings.internalSSID, "Lizas-Lamp");
    strcpy(defaultSettings.internalPW, "");
    strcpy(defaultSettings.externalSSID, "");
    strcpy(defaultSettings.externalPW, "");
    strcpy(defaultSettings.externalSSID2, "");
    strcpy(defaultSettings.externalPW2, "");
    defaultSettings.warningsEnabled = true;
    defaultSettings.deepSleepEnabled = true;

    return defaultSettings;
}

bool saveSystemSettings(const SystemSettings& settings) {
    return saveStruct(SYSTEM_SETTINGS_KEY, settings);
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

static const char* LAST_TIME_KEY = "lastKnownTime";

void saveLastKnownTime(time_t t) {
    if(!preferences.begin(PREF_NAMESPACE, false))
        return;
    preferences.putULong64(LAST_TIME_KEY, (uint64_t)t);
    preferences.end();
}

time_t loadLastKnownTime() {
    if(!preferences.begin(PREF_NAMESPACE, true))
        return 0;
    uint64_t t = preferences.getULong64(LAST_TIME_KEY, 0);
    preferences.end();
    return (time_t)t;
}


bool ensureSystemSettingsExistsAndResetIfNot() {
    serialPrint("Checking if SystemSettings exists...");
    return ensureStructExists(SYSTEM_SETTINGS_KEY, sizeof(SystemSettings), resetSystemSettings);
}
