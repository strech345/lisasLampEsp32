#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>

struct RANGE {
    int min;        // The minimum value for the setting.
    int max;        // The maximum value for the setting.
    int def;        // The default value for the setting.
    int cur;        // The current value of the setting.
    int eepromAddr; // The address in the EEPROM where this setting's value is
                    // stored.
};

struct RGB {
    byte r;
    byte g;
    byte b;
};

struct CustomColorState {
    RGB color;
    bool active;
};

struct Alarm {
    byte day; // 1-7 (Monday-Sunday), 0 = not set
    byte hour;
    byte minute;
    bool active;
};

#define MAX_ALARMS 10

// New struct to hold the full configuration
struct FullConfig {
    RGB color;
    byte brightnessMode;
    byte colorMode;     // 0=Cool White, 1=Neutral White, 2=Warm White, 3=Custom
    byte animationMode; // 0=Static, 1=Breathing
    Alarm alarms[MAX_ALARMS];
    uint16_t goodNightDuration; // in minutes
    uint16_t alarmDuration;     // in minutes
    uint16_t animationSpeed;
};

// Network settings used for internal AP and external STA connections
constexpr size_t SSID_MAX_LEN = 32;
constexpr size_t PWD_MAX_LEN = 64;

struct SystemSettings {
    char internalSSID[SSID_MAX_LEN + 1]; // nul-terminated
    char internalPW[PWD_MAX_LEN + 1];
    char externalSSID[SSID_MAX_LEN + 1];
    char externalPW[PWD_MAX_LEN + 1];
};

// Enum to identify the type of state that has changed
enum StateChangeType {
    STATE_CHANGE_ALARMS,
    STATE_CHANGE_CUSTOM_COLOR,
    STATE_CHANGE_CONFIG,
    STATE_CHANGE_SYSTEM_CONFIG
    // Add other state types here as needed
};

enum LampState {
    LAMP_STATE_DEFAULT = 0,
    LAMP_STATE_ALARM = 1,
    LAMP_STATE_GOOD_NIGHT = 2,
    LAMP_STATE_ERROR = 3,
    LAMP_STATE_SUCCESS = 4,
    LAMP_STATE_WARNING = 5,
    LAMP_STATE_SLEEP = 6
};

enum WiFiTestResult {
    WIFI_TEST_SUCCESS,
    WIFI_TEST_CONNECTION_FAILED,
    WIFI_TEST_NTP_FAILED,
    WIFI_TEST_SKIPPED_AP_ACTIVE,
    WIFI_TEST_IN_PROGRESS
};

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

struct WiFiTestTracker {
    WiFiTestResult lastDateResult = WIFI_TEST_CONNECTION_FAILED;
    unsigned long lastTestTime = 0;
    unsigned long lastSucceededTestTime = 0;
    unsigned long lastStaConnectionTime = 0;
    // unsigned long nextTestInterval =    30 * 60 * 1000; // 30 minutes in
    // milliseconds
    bool clockSynced = false;
    bool staConfigValid = false;
};

// Forward declarations for web server types
class AsyncWebServerRequest;

// Function pointer type for a generic state update callback
typedef void (*GenericStateUpdateCallback)(StateChangeType type, void* data);

// A route definition consisting of URI, HTTP method and handler
// Note: WebRequestMethod is defined in ESPAsyncWebServer.h
struct Route {
    const char* uri;
    int method; // Using int instead of WebRequestMethod to avoid forward
                // declaration issues
    void (*handler)(AsyncWebServerRequest*, const String&);
};

#endif // TYPES_H
