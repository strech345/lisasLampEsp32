#ifndef TYPES_H
#define TYPES_H

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
    byte colorMode;     // 0=Kaltweiß, 1=Neutralweiß, 2=Warmweiß, 3=Benutzerdefiniert
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
};

#define MAX_TSF_ENTRIES 10

struct TsfEntry {
    uint8_t bssid[6];
    uint64_t tsf_us;
    uint64_t unix_us;
};

// Art der Zustandsänderung — wird im Callback onStateUpdatedFromWifi verwendet
enum StateChangeType {
    STATE_CHANGE_CONFIG,
    STATE_CHANGE_SYSTEM_CONFIG,
    STATE_CHANGE_WARNINGS,
    STATE_CHANGE_DEEP_SLEEP
};

// Aktueller Betriebszustand der Lampe — bestimmt welcher LED-Effekt aktiv ist
enum LampState {
    LAMP_STATE_DEFAULT     = 0,
    LAMP_STATE_ALARM       = 1,
    LAMP_STATE_GOOD_NIGHT  = 2,
    LAMP_STATE_ERROR       = 3, // Temporärer Alert: rot blinkend
    LAMP_STATE_SUCCESS     = 4, // Temporärer Alert: grün
    LAMP_STATE_WARNING     = 5, // Temporärer Alert: gelb blinkend
    LAMP_STATE_SLEEP       = 6, // Lampe aus, wartet auf Nutzer-Eingriff
    LAMP_STATE_CELEBRATION = 7,
    LAMP_STATE_SYS_ERROR   = 8, // Dauerhaft: keine Systemzeit — einzelner Pixel blinkt rot
    LAMP_STATE_SYS_WARNING = 9  // Dauerhaft: NTP veraltet (> 24h) — einzelner Pixel blinkt gelb
};

// Ergebnis eines WiFi/NTP-Tests
enum WiFiTestResult {
    WIFI_TEST_SUCCESS,
    WIFI_TEST_CONNECTION_FAILED,
    WIFI_TEST_NTP_FAILED,
    WIFI_TEST_SKIPPED_AP_ACTIVE,
    WIFI_TEST_IN_PROGRESS
};

// Status-Snapshot für die Web-API
struct WiFiStatus {
    String currentTime;
    WiFiTestResult lastTestResult;
    unsigned long timeSinceLastTestMs;
    unsigned long timeSinceLastSucceededTestMs;
    unsigned long lastStaConnectionTime;
    unsigned long systemTime;
    bool clockSynced;
    bool staConfigValid;
};

// Interner Tracker für WiFi/NTP-Zustand (läuft im Hintergrund)
struct WiFiTestTracker {
    WiFiTestResult lastDateResult = WIFI_TEST_CONNECTION_FAILED;
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
