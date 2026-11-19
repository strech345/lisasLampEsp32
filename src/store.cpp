// Persistenzschicht — Implementierung
// Alle Struct-Daten werden als binäre Byte-Blöcke im NVS-Namespace "lisas_lamp_cfg" abgelegt.
#include "store.h"

#include <Preferences.h>

#include "debug_utils.h"

#include "types.h"
#include <Arduino.h>

// Zugangsdaten-Standardwerte: eigene Werte aus include/secrets.h (gitignored), sonst die Vorlage
#if __has_include("secrets.h")
#include "secrets.h"
#else
#include "secrets.example.h"
#endif

// Compile-Zeit-Prüfung der Standardwerte (sizeof eines String-Literals = Länge + 1).
// Verhindert eine Firmware, deren Hotspot nach einem Werksreset nicht starten könnte.
#define SECRET_LEN(s) (sizeof(s) - 1)
#define SECRET_PW_OK(s) (SECRET_LEN(s) == 0 || (SECRET_LEN(s) >= 8 && SECRET_LEN(s) <= 63))
static_assert(SECRET_LEN(SECRET_AP_SSID) >= 1 && SECRET_LEN(SECRET_AP_SSID) <= SSID_MAX_LEN,
              "secrets.h: SECRET_AP_SSID muss 1-32 Zeichen lang sein");
static_assert(SECRET_LEN(SECRET_EXT_SSID) <= SSID_MAX_LEN && SECRET_LEN(SECRET_EXT_SSID2) <= SSID_MAX_LEN,
              "secrets.h: externe SSIDs duerfen hoechstens 32 Zeichen lang sein");
static_assert(SECRET_PW_OK(SECRET_AP_PASSWORD) && SECRET_PW_OK(SECRET_EXT_PASSWORD)
                  && SECRET_PW_OK(SECRET_EXT_PASSWORD2),
              "secrets.h: Passwoerter muessen leer oder 8-63 Zeichen lang sein");

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

// Schreibt eine per Debounce ausstehende FullConfig sofort (no-op wenn nichts aussteht).
// Muss vor Deep Sleep und vor ESP.restart() aufgerufen werden, sonst geht die Änderung verloren.
void flushPendingSave() {
    if(lastSaveRequestTime == 0)
        return; // nichts ausstehend
    serialPrint("Flush: Saving pending FullConfig immediately");
    saveFullConfig(pendingConfig, false); // setzt lastSaveRequestTime wieder auf 0
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

// Gibt Systemeinstellungen mit Standardwerten zurück; Zugangsdaten aus secrets.h (siehe oben)
SystemSettings getDefaultSystemSettings() {
    SystemSettings defaultSettings;
    strlcpy(defaultSettings.internalSSID, SECRET_AP_SSID, sizeof(defaultSettings.internalSSID));
    strlcpy(defaultSettings.internalPW, SECRET_AP_PASSWORD, sizeof(defaultSettings.internalPW));
    strlcpy(defaultSettings.externalSSID, SECRET_EXT_SSID, sizeof(defaultSettings.externalSSID));
    strlcpy(defaultSettings.externalPW, SECRET_EXT_PASSWORD, sizeof(defaultSettings.externalPW));
    strlcpy(defaultSettings.externalSSID2, SECRET_EXT_SSID2, sizeof(defaultSettings.externalSSID2));
    strlcpy(defaultSettings.externalPW2, SECRET_EXT_PASSWORD2, sizeof(defaultSettings.externalPW2));
    defaultSettings.warningsEnabled = true;
    defaultSettings.deepSleepEnabled = true;
    defaultSettings.tsfFallbackEnabled = true;
    defaultSettings.rdsClockEnabled = false;
    defaultSettings.rdsLastChannel = 0;
    defaultSettings.rtcCrystalEnabled = false;
    defaultSettings.rtcCrystalAvailable = false;
    return defaultSettings;
}

// Speichert die Systemeinstellungen sofort in den Flash-Speicher
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

// Überschreibt die gespeicherten Systemeinstellungen mit den Standardwerten
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

// Speichert die TSF-Zeitstempel-Einträge (count Stück) in den Flash-Speicher
bool saveTsfEntries(const TsfEntry* entries, uint8_t count) {
    if (!preferences.begin(PREF_NAMESPACE, false)) return false;
    size_t written = preferences.putBytes(TSF_DATA_KEY, entries, count * sizeof(TsfEntry));
    preferences.end();
    return written == count * sizeof(TsfEntry);
}

// Lädt bis zu maxCount TSF-Einträge; gibt die Anzahl tatsächlich geladener Einträge zurück
uint8_t loadTsfEntries(TsfEntry* entries, uint8_t maxCount) {
    // Preferences nur einmal öffnen: Länge und Daten in derselben Sitzung lesen
    if (!preferences.begin(PREF_NAMESPACE, true)) return 0;
    size_t storedSize = preferences.getBytesLength(TSF_DATA_KEY);
    if (storedSize == 0 || storedSize % sizeof(TsfEntry) != 0) {
        preferences.end();
        return 0;
    }
    uint8_t count = storedSize / sizeof(TsfEntry);
    if (count > maxCount) count = maxCount;
    preferences.getBytes(TSF_DATA_KEY, entries, count * sizeof(TsfEntry));
    preferences.end();
    return count;
}

// Stellt sicher dass gültige SystemSettings im Flash vorhanden sind
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
