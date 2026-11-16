// Persistenzschicht — Implementierung
// Alle Struct-Daten werden als binäre Byte-Blöcke im NVS-Namespace "lisas_lamp_cfg" abgelegt.
#include "store.h"

#include <Preferences.h>

#include "debug_utils.h"

#include "types.h"
#include <Arduino.h>

static Preferences preferences;

const char* PREF_NAMESPACE = "lisas_lamp_cfg";
const char* CONFIG_KEY = "fullConfig";

// Generischer Helfer: speichert ein beliebiges Struct in den Flash-Speicher
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

// Speichert die Konfiguration; mit debounce=true wird der Schreibvorgang um 5 Sekunden verzögert
// (verhindert Flash-Verschleiß bei häufigem Drehen am Encoder)
bool saveFullConfig(const FullConfig& config, bool debounce) {
    if(debounce) {
        pendingConfig = config;
        lastSaveRequestTime = millis();
        return true;
    }
    lastSaveRequestTime = 0;
    return saveStruct(CONFIG_KEY, config);
}

// Führt verzögerten Schreibvorgang aus wenn 5 Sekunden seit letzter Änderung vergangen sind
void checkToSave() {
    if(lastSaveRequestTime != 0) {
        if(millis() - lastSaveRequestTime >= 5000) {
            serialPrint("Debounced: Saving FullConfig after 5s");
            saveFullConfig(pendingConfig, false);
        }
    }
}

// Lädt die gespeicherte Konfiguration aus dem Flash-Speicher
bool loadFullConfig(FullConfig& config) {
    if(!preferences.begin(PREF_NAMESPACE, true)) {
        serialPrint("Failed to open preferences for reading");
        return false;
    }

    size_t bytesRead = preferences.getBytes(CONFIG_KEY, &config, sizeof(FullConfig));
    preferences.end();

    if(bytesRead == sizeof(FullConfig)) {
        serialPrint("FullConfig loaded successfully!");
        return true;
    } else {
        serialPrint("Failed to load FullConfig. Bytes read: " + String(bytesRead));
        return false;
    }
}

// Gibt eine Konfiguration mit Standardwerten zurück
FullConfig getDefaultFullConfig() {
    FullConfig defaultConfig;

    defaultConfig.color = {255, 255, 255};
    defaultConfig.colorMode = 1;      // Neutralweiß
    defaultConfig.animationMode = 1;  // Atmungs-Effekt
    defaultConfig.brightnessMode = 7; // Maximale Helligkeit

    for(int i = 0; i < MAX_ALARMS; ++i) {
        defaultConfig.alarms[i] = {0, 0, 0, false};
    }
    defaultConfig.goodNightDuration = 30;
    defaultConfig.alarmDuration = 30;
    defaultConfig.animationSpeed = 200;

    return defaultConfig;
}

// Überschreibt die gespeicherte Konfiguration mit den Standardwerten
bool resetFullConfig() {
    serialPrint("Resetting FullConfig to defaults...");
    FullConfig defaultConfig = getDefaultFullConfig();
    if(!saveFullConfig(defaultConfig, false)) {
        serialPrint("Failed to save default config during reset.");
        return false;
    }
    serialPrint("FullConfig successfully reset to defaults.");
    return true;
}

// Prüft ob ein Struct im Flash existiert und die richtige Größe hat; bei Abweichung Reset aufrufen
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
        // Größenänderung durch Firmware-Update → neu initialisieren
        return resetFn();
    }
    return true;
}

// Stellt sicher dass eine gültige FullConfig im Flash vorhanden ist
bool ensureConfigExistsAndResetIfNot() {
    serialPrint("Checking if FullConfig exists...");
    return ensureStructExists(CONFIG_KEY, sizeof(FullConfig), resetFullConfig);
}

const char* SYSTEM_SETTINGS_KEY = "systemSettings";

// Gibt Systemeinstellungen mit Standardwerten zurück (eigener AP, kein externes WLAN)
SystemSettings getDefaultSystemSettings() {
    SystemSettings defaultSettings;
    strcpy(defaultSettings.internalSSID, "Lizas-Lamp");
    strcpy(defaultSettings.internalPW, "");
    strcpy(defaultSettings.externalSSID, "");
    strcpy(defaultSettings.externalPW, "");
    strcpy(defaultSettings.externalSSID2, "");
    strcpy(defaultSettings.externalPW2, "");
    defaultSettings.warningsEnabled = true;
    defaultSettings.deepSleepEnabled = true;
    defaultSettings.tsfFallbackEnabled = true;
    defaultSettings.rdsClockEnabled = false;
    defaultSettings.rdsLastChannel = 0;
    defaultSettings.rtcCrystalEnabled = false;
    defaultSettings.rtcCrystalAvailable = false;
    defaultSettings.webPassword[0] = '\0';
    return defaultSettings;
}

bool saveSystemSettings(const SystemSettings& settings) {
    return saveStruct(SYSTEM_SETTINGS_KEY, settings);
}

// Lädt die Systemeinstellungen aus dem Flash-Speicher
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
        memset(&settings, 0, sizeof(SystemSettings));
        return false;
    }
}

bool resetSystemSettings() {
    serialPrint("Resetting SystemSettings to defaults...");
    SystemSettings defaultSettings = getDefaultSystemSettings();
    if(!saveSystemSettings(defaultSettings)) {
        serialPrint("Failed to save default system settings during reset.");
        return false;
    }
    serialPrint("SystemSettings successfully reset to defaults.");
    return true;
}

static const char* TSF_DATA_KEY = "tsfData";

bool saveTsfEntries(const TsfEntry* entries, uint8_t count) {
    if (!preferences.begin(PREF_NAMESPACE, false)) return false;
    size_t written = preferences.putBytes(TSF_DATA_KEY, entries, count * sizeof(TsfEntry));
    preferences.end();
    return written == count * sizeof(TsfEntry);
}

uint8_t loadTsfEntries(TsfEntry* entries, uint8_t maxCount) {
    if (!preferences.begin(PREF_NAMESPACE, true)) return 0;
    size_t storedSize = preferences.getBytesLength(TSF_DATA_KEY);
    preferences.end();
    if (storedSize == 0 || storedSize % sizeof(TsfEntry) != 0) return 0;
    uint8_t count = storedSize / sizeof(TsfEntry);
    if (count > maxCount) count = maxCount;
    if (!preferences.begin(PREF_NAMESPACE, true)) return 0;
    preferences.getBytes(TSF_DATA_KEY, entries, count * sizeof(TsfEntry));
    preferences.end();
    return count;
}

static const char* AUTH_TOKENS_KEY = "authTokens";

bool saveAuthTokens(const AuthTokenStore& store) {
    return saveStruct(AUTH_TOKENS_KEY, store);
}

// Lädt gespeicherte Auth-Tokens; gibt false zurück wenn noch keine vorhanden
bool loadAuthTokens(AuthTokenStore& store) {
    if(!preferences.begin(PREF_NAMESPACE, true)) {
        memset(&store, 0, sizeof(AuthTokenStore));
        return false;
    }
    size_t bytesRead = preferences.getBytes(AUTH_TOKENS_KEY, &store, sizeof(AuthTokenStore));
    preferences.end();
    if(bytesRead == sizeof(AuthTokenStore)) return true;
    memset(&store, 0, sizeof(AuthTokenStore));
    return false;
}

bool ensureSystemSettingsExistsAndResetIfNot() {
    serialPrint("Checking if SystemSettings exists...");
    return ensureStructExists(SYSTEM_SETTINGS_KEY, sizeof(SystemSettings), resetSystemSettings);
}

static const char* LAST_TIME_KEY = "lastKnownTime";

// Speichert den aktuellen Unix-Timestamp damit er nach einem Neustart ohne Internet wiederhergestellt werden kann
void saveLastKnownTime(time_t t) {
    if(!preferences.begin(PREF_NAMESPACE, false))
        return;
    preferences.putULong64(LAST_TIME_KEY, (uint64_t)t);
    preferences.end();
}

// Gibt den zuletzt gespeicherten Unix-Timestamp zurück (0 wenn noch nie gespeichert)
time_t loadLastKnownTime() {
    if(!preferences.begin(PREF_NAMESPACE, true))
        return 0;
    uint64_t t = preferences.getULong64(LAST_TIME_KEY, 0);
    preferences.end();
    return (time_t)t;
}
