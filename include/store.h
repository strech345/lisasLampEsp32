#ifndef STORE_H
#define STORE_H
// Persistenzschicht: liest und schreibt FullConfig, SystemSettings, TSF-Daten und den letzten bekannten Zeitstempel
// in den Flash-Speicher des ESP32 über die Preferences-Bibliothek (NVS).

#include "types.h" // For FullConfig definition

// Speichert FullConfig in NVS; debounce=true verzögert den Schreibvorgang
bool saveFullConfig(const FullConfig& config, bool debounce = false);
// Führt den verzögerten Schreibvorgang aus (aus der Hauptschleife aufrufen)
void checkToSave();
// Schreibt eine ausstehende (debounced) FullConfig sofort; no-op wenn nichts aussteht.
// Vor Deep Sleep und vor ESP.restart() aufrufen.
void flushPendingSave();

// Liefert FullConfig mit Standardwerten
FullConfig getDefaultFullConfig();

// Setzt FullConfig in NVS auf Standardwerte zurück
bool resetFullConfig();

// Speichert SystemSettings sofort in NVS
bool saveSystemSettings(const SystemSettings& settings);

// Liefert SystemSettings mit Standardwerten
SystemSettings getDefaultSystemSettings();

// Setzt SystemSettings in NVS auf Standardwerte zurück
bool resetSystemSettings();

// Laden mit Fallback: fehlt der Eintrag oder passt die Größe nicht, werden Standardwerte gespeichert
void loadFullConfigOrDefault(FullConfig& config);
void loadSystemSettingsOrDefault(SystemSettings& settings);

// Speichert letzten bekannten Unix-Timestamp für Wiederherstellung nach Neustart
void saveLastKnownTime(time_t t);
time_t loadLastKnownTime();

bool saveTsfEntries(const TsfEntry* entries, uint8_t count);
uint8_t loadTsfEntries(TsfEntry* entries, uint8_t maxCount);

#endif // STORE_H
