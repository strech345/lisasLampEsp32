

#include "alarm.h"
#include <Preferences.h>
#include <string.h>            // For memcpy
#include <time.h>              // For time functions
#include "preferences_utils.h" // For generic get/put functions
#include "rgb_effects.h"       // For sunrise_fade
#include "debug_utils.h"       // For serialPrint

#define MAX_ALARMS 10
// ...existing code...

// === END OF FILE: TimeInfo struct and getCurrentTimeInfo implementation ===
struct TimeInfo {
    int hour;
    int minute;
    int dayOfWeek; // 1-7 (Sun=7)
    int day;
};

TimeInfo getCurrentTimeInfo();

Alarm alarms[MAX_ALARMS];

/* void setup_alarms() {
    Preferences preferences;
    preferences.begin("alarms", true);  // read-only
    preferences.getBytes("alarms_data", alarms,
sizeof(alarms)); preferences.end();
}

void save_alarms() {
    Preferences preferences;
    preferences.begin("alarms", false);
    // Use the generic putValue. It handles the check to see
if the data has changed. putValue(preferences,
"alarms_data", alarms); preferences.end();
} */

// === ALARM LOGIC ===

// A volatile array to track the day of the year an alarm
// was last triggered. This is NOT saved to flash and will
// reset on boot.
static int last_triggered_day_for_alarm[MAX_ALARMS];

static int activeAlarmId = -1; // -1 means no alarm is active. Otherwise, it's
                               // the index in the alarms array.
static unsigned long alarm_start_millis =
    0; // Stores the millis() time when the alarm started.

#define WAKEUP_DURATION_MINUTES 2

/**
 * @brief Returns a pointer to the global alarms array.
 * @return A pointer to the alarms array.
 */
Alarm* getAlarms() {
    return alarms;
}

/**
 * @brief Overwrites the current alarms with a new set and
 * saves them to persistent storage.
 * @param newAlarms An array containing the new alarm data.
 * Must have MAX_ALARMS elements.
 */
void setAlarms(const Alarm newAlarms[MAX_ALARMS]) {
    resetAlarms();
    // Copy the new alarm data into the global alarms array
    memcpy(alarms, newAlarms, sizeof(alarms));

    // Save the newly updated alarms to persistent storage
    // save_alarms();
}

/**
 * @brief Returns the index of the currently active alarm.
 * @return The index (0-9) of the active alarm, or -1 if no
 * alarm is active.
 */
int getActiveAlarmIndex() {
    return activeAlarmId;
}

bool isAlarmActive() {
    return activeAlarmId != -1;
}

/**
 * @brief Sets the active alarm index and records the start
 * time.
 * @param index The index of the alarm to set as active, or
 * -1 to deactivate all.
 */
void setActiveAlarm(int index) {
    if(index < -1 || index >= MAX_ALARMS || activeAlarmId == index) {
        return;
    }

    /* if(activeAlarmId != -1) {
        serialPrint("Deactivating alarm: " + String(activeAlarmId));
        last_triggered_day_for_alarm[activeAlarmId] = -1;
    } */
    activeAlarmId = index;

    if(activeAlarmId != -1) {
        TimeInfo currentTimeInfo = getCurrentTimeInfo();
        last_triggered_day_for_alarm[activeAlarmId] = currentTimeInfo.day;
        alarm_start_millis = millis(); // Record the start time of
                                       // the alarm animation
    }
    serialPrint("Active alarm set to: " + String(activeAlarmId));
}

/**
 * @brief Disables any currently active alarm and marks it
 * as handled for today.
 */
void stopActiveAlarm() {
    if(activeAlarmId != -1) {
        setActiveAlarm(-1);
    }
}

/**
 * @brief Checks alarms against the internal RTC time.
 * This function should be called periodically from the main
 * loop.
 */
void checkAlarmStates() {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    // Only check every 10 seconds
    if(currentTime - lastCheck < 10000) {
        return;
    }
    lastCheck = currentTime;

    int runningMs = millis() - alarm_start_millis;

    TimeInfo currentTimeInfo = getCurrentTimeInfo();
    // First, check if a currently active alarm needs to be disabled
    if(activeAlarmId != -1 && runningMs > WAKEUP_DURATION_MINUTES * 60 * 1000) {
        serialPrint("Stopping active alarm after duration here : "
                    + String(activeAlarmId));
        stopActiveAlarm();
    }

    // Activate alarm
    for(int i = 0; i < MAX_ALARMS; i++) {
       /*  if (alarms[i].active) {
             serialPrint("Alarm[" + String(i) + "] Debug: " +
                        "CurrDay=" + String(currentTimeInfo.day) +
                        " vs LastTrig=" + String(last_triggered_day_for_alarm[i]) +
                        ", AlmDay=" + String(alarms[i].day) +
                        " vs CurrDoW=" + String(currentTimeInfo.dayOfWeek) +
                        ", AlmHr=" + String(alarms[i].hour) +
                        " vs CurrHr=" + String(currentTimeInfo.hour) +
                        ", AlmMin=" + String(alarms[i].minute) +
                        " vs CurrMin=" + String(currentTimeInfo.minute));
        } */
        if(i != activeAlarmId && alarms[i].active
           && currentTimeInfo.day != last_triggered_day_for_alarm[i]
           && alarms[i].day == currentTimeInfo.dayOfWeek
           && alarms[i].hour == currentTimeInfo.hour
           && alarms[i].minute == currentTimeInfo.minute) {
            serialPrint("Alarm[" + String(i) + "]: day=" + String(alarms[i].day)
                        + ", hour=" + String(alarms[i].hour) + ", minute="
                        + String(alarms[i].minute) + ", last_triggered="
                        + String(last_triggered_day_for_alarm[i]));

            setActiveAlarm(i);
            return;
        }
    }
}
/**
 * @brief If an alarm is active, calculates the RGB color
 * based on elapsed animation time.
 * @param currentMillis The current time from millis() for
 * high-precision animation.
 * @param color A reference to the RGB struct to be filled.
 * @return true if an alarm is active and a color was
 * calculated, false otherwise.
 */
bool getAlarmColor(unsigned long currentMillis, RGB& color) {
    if(activeAlarmId == -1) {
        return false;
    }

    // Calculate how many milliseconds have passed since the
    // alarm animation started
    unsigned long elapsed_millis = currentMillis - alarm_start_millis;
    const unsigned long wakeup_duration_millis =
        WAKEUP_DURATION_MINUTES * 60 * 1000;

    // The check_alarms() function is responsible for
    // stopping the alarm after its duration. This
    // calculation just determines the color for the current
    // moment in the animation.
    if(elapsed_millis > wakeup_duration_millis) {
        // As a fallback, show the final color if we are
        // past the duration
        color = sunriseFade(1.0);
        return true;
    }

    // Calculate the progress of the wakeup effect (0.0
    // to 1.0)
    float progress = (float)elapsed_millis / wakeup_duration_millis;

    // Get the color from the RGB effects function
    color = sunriseFade(progress);
    return true;
}

/**
 * @brief Gets the interpolated brightness for the current alarm.
 * Starts at 1 and ramps to 7 over 70% of the alarm duration.
 * @return Brightness value from 1 to 7, or 0 if no alarm is active.
 */
byte getAlarmBrightness() {
    if(activeAlarmId == -1) {
        return 0;
    }
    unsigned long elapsed_millis = millis() - alarm_start_millis;
    const unsigned long total_duration_millis =
        WAKEUP_DURATION_MINUTES * 60 * 1000;
    const unsigned long brightness_duration_millis =
        (total_duration_millis * 70) / 100;
    if(elapsed_millis >= brightness_duration_millis) {
        return 7; // Maximum brightness after 70% of duration
    }
    float progress = (float)elapsed_millis / brightness_duration_millis;
    byte brightness = 1 + (byte)(progress * (7 - 1));
    serialPrint("Alarm brightness calculated: " + String(brightness) + " (progress: " + String(progress) + ")");
    return brightness;
}

void resetAlarms() {
    serialPrint("Resetting all alarms and active states");
    stopActiveAlarm();
    for(int i = 0; i < MAX_ALARMS; ++i) {
        last_triggered_day_for_alarm[i] = -1;
    }
    activeAlarmId = -1;
    alarm_start_millis = 0;
}

/**
 * @brief Returns current hour, minute, and dayOfWeek for now (no input param).
 * @return TimeInfo struct
 */
TimeInfo getCurrentTimeInfo() {
    time_t now;
    time(&now);
    struct tm* ptm = localtime(&now);
    TimeInfo info;
    info.hour = ptm->tm_hour;
    info.minute = ptm->tm_min;
    info.dayOfWeek = ptm->tm_wday == 0 ? 7 : ptm->tm_wday;
    info.day = ptm->tm_yday;
    return info;
}

/**
 * @brief Returns the time in milliseconds until the next active alarm event.
 * If no alarm is set, returns -1.
 */
long getMillisToNextAlarm() {
    TimeInfo now = getCurrentTimeInfo();
    long minMillis = -1;
    for(int i = 0; i < MAX_ALARMS; i++) {
        const Alarm& alarm = alarms[i];
        if(!alarm.active || alarm.day == 0)
            continue;

        // Calculate days until next occurrence
        int daysDelta = alarm.day - now.dayOfWeek;
        if(daysDelta < 0)
            daysDelta += 7;

        // Calculate time for the alarm event
        int alarmTotalMinutes = alarm.hour * 60 + alarm.minute;
        int nowTotalMinutes = now.hour * 60 + now.minute;

        long millisToAlarm = daysDelta * 24L * 60L * 60L * 1000L;
        millisToAlarm += (alarmTotalMinutes - nowTotalMinutes) * 60L * 1000L;

        // If alarm is today but time already passed, roll to next week
        if(daysDelta == 0 && alarmTotalMinutes <= nowTotalMinutes) {
            millisToAlarm += 7L * 24L * 60L * 60L * 1000L;
        }

        // If negative, skip
        if(millisToAlarm < 0)
            continue;
        if(minMillis == -1 || millisToAlarm < minMillis) {
            minMillis = millisToAlarm;
        }
    }
    return minMillis;
}

bool hasActiveAlarms() {
    for(int i = 0; i < MAX_ALARMS; i++) {
        if(alarms[i].active && alarms[i].day != 0) {
            return true;
        }
    }
    return false;
}