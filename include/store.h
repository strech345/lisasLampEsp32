#ifndef STORE_H
#define STORE_H
// Persistenzschicht: liest und schreibt FullConfig, SystemSettings, TSF-Daten und Auth-Tokens
// in den Flash-Speicher des ESP32 über die Preferences-Bibliothek (NVS).

#include "types.h" // For FullConfig definition

// Speichert FullConfig in NVS; debounce=true verzögert den Schreibvorgang
bool saveFullConfig(const FullConfig& config, bool debounce = false);
// Führt den verzögerten Schreibvorgang aus (aus der Hauptschleife aufrufen)
void checkToSave();

// Lädt FullConfig aus NVS; gibt false zurück wenn kein gültiger Eintrag vorhanden
bool loadFullConfig(FullConfig& config);

// Liefert FullConfig mit Standardwerten
FullConfig getDefaultFullConfig();

// Setzt FullConfig in NVS auf Standardwerte zurück
bool resetFullConfig();

// Stellt sicher dass FullConfig in NVS existiert, sonst resetFullConfig()
bool ensureConfigExistsAndResetIfNot();

bool saveSystemSettings(const SystemSettings& settings);
bool loadSystemSettings(SystemSettings& settings);

// Liefert SystemSettings mit Standardwerten
SystemSettings getDefaultSystemSettings();

// Setzt SystemSettings in NVS auf Standardwerte zurück
bool resetSystemSettings();

// Stellt sicher dass SystemSettings in NVS existiert, sonst resetSystemSettings()
bool ensureSystemSettingsExistsAndResetIfNot();

// Speichert letzten bekannten Unix-Timestamp für Wiederherstellung nach Neustart
void saveLastKnownTime(time_t t);
time_t loadLastKnownTime();

bool saveTsfEntries(const TsfEntry* entries, uint8_t count);
uint8_t loadTsfEntries(TsfEntry* entries, uint8_t maxCount);

#endif // STORE_H
