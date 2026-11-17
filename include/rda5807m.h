#ifndef RDA5807M_H
#define RDA5807M_H

#include <Arduino.h>
#include <time.h>

// GPIO-Pinbelegung — an die eigene Hardwareplanung anpassen
#define RDA_I2C_SDA_PIN   4
#define RDA_I2C_SCL_PIN   5
#define RDA_POWER_GPIO    6   // HIGH = Modul bekommt Strom

// Europäisches FM-Band 87,0–108,0 MHz, 100 kHz Rasterabstand
#define RDA_BAND_START_KHZ   87000u
#define RDA_BAND_END_KHZ    108000u
#define RDA_STEP_KHZ          100u

// RSSI-Mindestpegel (0–127) damit ein Sender als brauchbar gilt
#define RDA_RSSI_THRESHOLD   25

// Maximale Wartezeit pro Sender auf RDS-Zeitdaten (ms)
#define RDA_RDS_TIMEOUT_MS   30000u

// Wiederholungsabstand wenn kein Sender eine Uhrzeit liefern konnte (ms)
#define RDA_RETRY_INTERVAL_MS (30ul * 60ul * 1000ul)

// Gespeicherte Sender (bestmögliche Kanäle aus bisherigen Scans)
#define RDA_MAX_SAVED_CHANNELS 8

struct RdaSavedChannels {
    uint8_t  count;
    uint16_t freq_100khz[RDA_MAX_SAVED_CHANNELS]; // z. B. 1015 = 101,5 MHz
};

// Aktueller Status für die Web-API
struct RdaStatus {
    bool    moduleEnabled;
    bool    active;              // Modul ist eingeschaltet und läuft
    int32_t currentFreq_kHz;    // aktuell abgestimmter Sender (0 = keiner)
    uint8_t rssi;               // Signalstärke des aktuellen Senders (0–127)
    bool    rdsTimeValid;       // true solange das letzte RDS-Sync frisch ist
    uint32_t lastSyncMs;        // millis() des letzten erfolgreichen RDS-Syncs
};

// Einmalige Initialisierung: I2C-Bus und Power-GPIO konfigurieren
// pEnabled zeigt auf systemSettings.rda5807mEnabled
void initRda5807m(const bool* pEnabled);

// Muss in loop() aufgerufen werden.
// ntpSynced=true  → Modul abschalten (NTP liefert die Zeit)
// ntpSynced=false → Modul starten und RDS-Uhrzeit suchen
void rdaLoop(bool ntpSynced);

// Status für die Web-API
RdaStatus getRdaStatus();

#endif // RDA5807M_H
