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

// Ein Wecker-Eintrag: wöchentlich (Wochentag) oder einmalig (Datum), Uhrzeit, aktiv/inaktiv.
// Ein Platz ist belegt, wenn day > 0 (wöchentlich) oder once (einmalig).
struct Alarm {
    byte day;    // wöchentlich: 1–7 (Montag–Sonntag); 0 = kein wöchentlicher Wecker
    byte hour;
    byte minute;
    bool active;
    bool once;          // true = einmaliger Termin am Datum unten (day wird ignoriert)
    byte onceDay;       // 1–31
    byte onceMonth;     // 1–12
    uint16_t onceYear;  // z. B. 2026
};

#define MAX_ALARMS 16

// Anzahl der LEDs im Streifen (in der Weboberfläche einstellbar, gespeichert in SystemSettings)
#define DEFAULT_LED_COUNT 4
#define MAX_LED_COUNT 16 // Strombedarf: bis ca. 80 mA pro RGBW-LED bei voller Helligkeit

// Ein Szene: Lichtfarbe + Animation (in der Weboberfläche anlegbar, per Drücken + Drehen wählbar)
struct LightSet {
    byte colorMode;     // 0=Kaltweiß, 1=Neutralweiß, 2=Warmweiß, 3=Eigene Farbe, 4=Kerzenlicht
    byte animationMode; // 0=Statisch, 1=Atmen, 2=Flamme
    RGB color;          // nur bei colorMode 3 (Eigene Farbe)
};

#define MAX_LIGHT_SETS 6

// Vollständige Lampen-Konfiguration (wird im Flash gespeichert)
struct FullConfig {
    LightSet sets[MAX_LIGHT_SETS];
    byte setCount;  // Anzahl angelegter Sets (1–MAX_LIGHT_SETS)
    byte activeSet; // Index des aktiven Sets (0 … setCount-1)
    byte brightnessMode;
    Alarm alarms[MAX_ALARMS];
    uint16_t goodNightDuration; // Dauer Gute-Nacht-Modus in Minuten
    uint16_t alarmDuration;     // Dauer Weckalarm in Minuten
    uint16_t animationSpeed;
};

// Maximale Längen für SSID und Passwort (WiFi-Spezifikation)
constexpr size_t SSID_MAX_LEN = 32;
constexpr size_t PWD_MAX_LEN = 64;

// Ein Party-Tag (Feier-Modus ab 7:00 Uhr), jedes Jahr wiederkehrend
struct CelebrationDay {
    uint8_t month; // 1–12
    uint8_t day;   // 1–31
};

#define MAX_CELEBRATIONS 8
#define LAMP_NAME_MAX_LEN 20

// Hotspot-Laufzeit und Zeitabgleich (Minuten, in der Weboberfläche einstellbar).
// Standardwerte = bisheriges festes Verhalten: Hotspot 10 min ohne Bedienung, Zeitabgleich alle 5 h.
#define DEFAULT_AP_TIMEOUT_MIN 10      // 0 = Hotspot bleibt an, bis er von Hand ausgeschaltet wird
#define MAX_AP_TIMEOUT_MIN 1440        // höchstens 24 h
#define DEFAULT_SYNC_INTERVAL_MIN 300  // nach erfolgreichem Abgleich: alle 5 h
#define DEFAULT_SYNC_RETRY_MIN 300     // nach Fehlschlag: neuer Versuch nach 5 h
#define MIN_SYNC_INTERVAL_MIN 5        // kürzer lohnt nicht (Deep Sleep braucht ≥ 2 min Fenster)
#define MAX_SYNC_INTERVAL_MIN 720      // höchstens 12 h: NTP-Zeit gilt nach 24 h als veraltet

// Systemeinstellungen: eigener Hotspot (AP), zwei externe WLANs (mit Fallback), Schalter,
// persönliche Daten (Name, Party-Tage), Debug-Optionen und Zeiten für Hotspot/Zeitabgleich.
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
    bool rtcCrystalAvailable; // Quarz vorhanden (bei jedem Einschalten gemessen, intern gesetzt)
    uint8_t ledCount;         // Anzahl der LEDs im Streifen (1–MAX_LED_COUNT)
    // --- Persönlich (Standardwerte aus secrets.h) ---
    char lampName[LAMP_NAME_MAX_LEN + 1];            // Name der Lampe, z. B. "Liza"
    uint8_t celebrationCount;                         // Anzahl Party-Tage (0–MAX_CELEBRATIONS)
    CelebrationDay celebrations[MAX_CELEBRATIONS];
    // --- Debug ---
    bool statusLedEnabled; // blaue Status-LED auf dem Board
    bool serialLogEnabled; // serielle Logs (serialPrint)
    bool eventLogEnabled;  // Ereignisprotokoll aufzeichnen
    bool logToUsb;         // serielle Logs über USB statt UART (RX/TX)
    // --- Hotspot und Zeitabgleich (Minuten) ---
    uint16_t apTimeoutMin;    // Hotspot aus nach so vielen Minuten ohne Bedienung; 0 = unbegrenzt
    uint16_t syncIntervalMin; // nächster Zeitabgleich nach erfolgreichem Sync
    uint16_t syncRetryMin;    // nächster Versuch, wenn der letzte Sync fehlgeschlagen ist
};

#define MAX_TSF_ENTRIES 16

// TSF-Bezugspunkt eines Access Points: Zählerstand und Uhrzeit zum selben Moment.
// gen = Herkunft der Uhrzeit: 0 = direkt aus NTP (genauester Anker), n > 0 = aus einer TSF-Berechnung
// neu basiert, die Bezüge der Generation n−1 (oder besser) benutzt hat. Damit lassen sich abgeleitete
// Bezüge von NTP-Ankern trennen, und Fehler addieren sich nicht unbemerkt über mehrere Berechnungen.
// Gangkorrektur: drift_ppb = wie viel schneller (+) oder langsamer (−) der AP-Zähler gegenüber echter Zeit
// läuft, in Milliardstel. Gemessen zwischen zwei NTP-Snapshots (Messpunkt rate_*, Abstand ≥ 2 h).
#define TSF_DRIFT_UNKNOWN INT32_MIN
#define TSF_DRIFT_HIST 4 // so viele Gangmessungen je AP bleiben gespeichert (zur Beobachtung der Stabilität)

struct TsfEntry {
    uint8_t  bssid[6];
    uint64_t tsf_us;       // TSF-Wert zum Messzeitpunkt (Mikrosekunden)
    uint64_t unix_us;      // Unix-Zeit zum Messzeitpunkt (Mikrosekunden)
    int8_t   rssi;         // Empfangsstärke bei der Messung (dBm)
    uint8_t  gen;          // Generation (siehe oben)
    int32_t  drift_ppb;    // Gangabweichung des AP-Zählers; TSF_DRIFT_UNKNOWN = noch nicht gemessen
    uint64_t rate_tsf_us;  // NTP-Messpunkt für die nächste Gangmessung: Zählerstand …
    uint64_t rate_unix_us; // … und Unix-Zeit dazu; 0 = kein gültiger Messpunkt (z. B. nach AP-Neustart)
    int32_t  drift_hist[TSF_DRIFT_HIST]; // letzte gemessene Gänge (ppb), neueste zuerst; TSF_DRIFT_UNKNOWN = leer
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
    STATE_CHANGE_FACTORY_RESET, // (nullptr) verzögerter Werksreset + Neustart
    STATE_CHANGE_LED_COUNT,     // (uint8_t*) Anzahl der LEDs, wird sofort angewendet und gespeichert
    STATE_CHANGE_STATUS_LED,    // (bool*) Debug: Status-LED an/aus
    STATE_CHANGE_SERIAL_LOG,    // (bool*) Debug: serielle Logs an/aus
    STATE_CHANGE_EVENT_LOG,     // (bool*) Debug: Ereignisprotokoll aufzeichnen
    STATE_CHANGE_LOG_USB        // (bool*) Debug: serielle Logs über USB (sonst UART RX/TX)
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
    bool staConfigValid;  // Heimnetz verbunden (mit IP) — nur kurz während des Abgleichs
    bool staActive;       // Heimnetz gerade an (Verbindung oder Aufbau)
    bool lastSyncOk;      // letzter Abgleich-Versuch erfolgreich
    bool everSynced;      // seit dem Start mindestens einmal per NTP abgeglichen
    long nextSyncInMs;    // Restzeit bis zum nächsten Abgleich; 0 = läuft gerade, -1 = kein Heimnetz
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
