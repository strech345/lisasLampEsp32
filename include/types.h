#ifndef TYPES_H
#define TYPES_H
// Zentrale Typdefinitionen: Datenstrukturen für Konfiguration, Zustand, WiFi und Routen.

#include <Arduino.h>

// Farbe als 3 Byte (Rot, Grün, Blau)
struct RGB {
    byte r;
    byte g;
    byte b;
};

// Ein Wecker-Eintrag: Wochentag 1–7 (Mo–So), Uhrzeit, aktiv/inaktiv
struct Alarm {
    byte day;    // 1–7 (Montag–Sonntag), 0 = nicht gesetzt
    byte hour;
    byte minute;
    bool active;
};

#define MAX_ALARMS 10

// Vollständige Lampen-Konfiguration (wird im Flash gespeichert)
struct FullConfig {
    RGB color;
    byte brightnessMode;
    byte colorMode;     // 0=Kaltweiß, 1=Neutralweiß, 2=Warmweiß, 3=Benutzerdefiniert, 4=Kerzenlicht
    byte animationMode; // 0=Statisch, 1=Atmung
    Alarm alarms[MAX_ALARMS];
    uint16_t goodNightDuration; // Dauer Gute-Nacht-Modus in Minuten
    uint16_t alarmDuration;     // Dauer Weckalarm in Minuten
    uint16_t animationSpeed;
};

// Maximale Längen für SSID und Passwort (WiFi-Spezifikation)
constexpr size_t SSID_MAX_LEN = 32;
constexpr size_t PWD_MAX_LEN = 64;

// Netzwerk-Einstellungen: eigener Hotspot (AP) und zwei externe WLANs (mit Fallback)
struct SystemSettings {
    char internalSSID[SSID_MAX_LEN + 1]; // Eigener Hotspot-Name
    char internalPW[PWD_MAX_LEN + 1];
    char externalSSID[SSID_MAX_LEN + 1]; // Primäres externes WLAN
    char externalPW[PWD_MAX_LEN + 1];
    char externalSSID2[SSID_MAX_LEN + 1]; // Fallback-WLAN (optional)
    char externalPW2[PWD_MAX_LEN + 1];
    bool warningsEnabled;
    bool deepSleepEnabled;
    bool tsfFallbackEnabled;
    bool rdsClockEnabled;
    uint16_t rdsLastChannel; // Frequenz in 10kHz-Einheiten, z.B. 1011 = 101.1 MHz; 0 = nicht gesetzt
    bool rtcCrystalEnabled;  // Benutzer-Einstellung: externen 32-kHz-Quarz verwenden
    bool rtcCrystalAvailable; // Letzter Boot-Befund: Quarz war messbar (wird intern gesetzt)
};

#define MAX_TSF_ENTRIES 16

struct TsfEntry {
    uint8_t  bssid[6];
    uint64_t tsf_us;   // TSF-Wert zum Messzeitpunkt (Mikrosekunden)
    uint64_t unix_us;  // Unix-Zeit zum Messzeitpunkt (Mikrosekunden)
    int8_t   rssi;     // Empfangsstärke bei der Messung (dBm)
};

// Art der Zustandsänderung / des Auftrags vom Webserver an die loop().
// Der Callback queueWebCommand (web_commands.cpp) läuft im async_tcp-Task und legt nur einen Auftrag
// ab; ausgeführt wird er in loop(). Datentyp von `data` je Eintrag in Klammern.
enum StateChangeType {
    STATE_CHANGE_CONFIG,        // (const FullConfig*) neue Lampenkonfiguration
    STATE_CHANGE_SYSTEM_CONFIG, // (const SystemSettings*) Netzwerk-Einstellungen -> WLAN-Neustart
    STATE_CHANGE_WARNINGS,      // (bool*) Systemwarnungen an/aus
    STATE_CHANGE_DEEP_SLEEP,    // (bool*) Deep Sleep an/aus
    STATE_CHANGE_TSF_FALLBACK,  // (bool*) TSF-Uhrzeit-Fallback an/aus (kein WLAN-Neustart)
    STATE_CHANGE_RDS_CLOCK,     // (bool*) RDS-Uhrzeit an/aus (kein WLAN-Neustart)
    STATE_CHANGE_RTC_CRYSTAL,   // (bool*) RTC-Quarz an/aus, danach verzögerter Neustart
    STATE_CHANGE_NTP_RESYNC,    // (nullptr) NTP-Resync auslösen
    STATE_CHANGE_TSF_SNAPSHOT,  // (nullptr) TSF-Snapshot auslösen
    STATE_CHANGE_RESTART,       // (nullptr) verzögerter Neustart
    STATE_CHANGE_FACTORY_RESET  // (nullptr) verzögerter Werksreset + Neustart
};

// Aktueller Betriebszustand der Lampe — bestimmt welcher LED-Effekt aktiv ist
enum LampState {
    LAMP_STATE_DEFAULT     = 0,
    LAMP_STATE_ALARM       = 1,
    LAMP_STATE_GOOD_NIGHT  = 2,
    LAMP_STATE_ERROR       = 3, // Kurzer Hinweis: rot
    LAMP_STATE_SUCCESS     = 4, // Kurzer Hinweis: grün
    LAMP_STATE_WARNING     = 5, // Kurzer Hinweis: gelb
    LAMP_STATE_SLEEP       = 6, // Lampe aus, wartet auf Nutzer-Eingriff
    LAMP_STATE_CELEBRATION = 7,
    LAMP_STATE_SYS_ERROR   = 8, // Dauerhaft (nur bei Lampe an): keine Systemzeit — Lampe rot
    LAMP_STATE_SYS_WARNING = 9, // Dauerhaft (nur bei Lampe an): Zeitquelle > 24 h alt — Lampe gelb
    LAMP_STATE_AP_WAITING  = 10 // Hotspot an, noch kein Gerät verbunden — blauer Komet
};

// Status-Snapshot für die Web-API
struct WiFiStatus {
    String currentTime;
    unsigned long timeSinceLastTestMs;
    unsigned long timeSinceLastSucceededTestMs;
    unsigned long lastStaConnectionTime;
    unsigned long systemTime;
    bool clockSynced;
    bool staConfigValid;
};

// Interner Tracker für WiFi/NTP-Zustand (läuft im Hintergrund)
struct WiFiTestTracker {
    unsigned long lastTestTime = 0;
    unsigned long lastSucceededTestTime = 0;
    unsigned long lastStaConnectionTime = 0;
    bool clockSynced = false;
    bool staConfigValid = false;
};

// Vorwärts-Deklaration für den Webserver
class AsyncWebServerRequest;

// Callback-Typ für Zustandsänderungen aus dem Webserver
typedef void (*GenericStateUpdateCallback)(StateChangeType type, void* data);

// Eine Route: URL-Pfad, HTTP-Methode und Handler-Funktion
struct Route {
    const char* uri;
    int method;
    void (*handler)(AsyncWebServerRequest*, const String&);
};

#endif // TYPES_H
