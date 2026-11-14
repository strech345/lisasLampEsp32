#include "alarm.h"
#include <string.h>
#include <time.h>
#include "rgb_effects.h"
#include "debug_utils.h"

// Hilfsstruct: aktuelle Uhrzeit und Wochentag
struct TimeInfo {
    int hour;
    int minute;
    int dayOfWeek; // 1–7 (Mo–So)
    int day;       // Tag des Jahres (0–365), zur Entdopplung genutzt
};

TimeInfo getCurrentTimeInfo();

Alarm alarms[MAX_ALARMS];

// --- Alarmstatus ---

// Speichert den Jahrestag (tm_yday) des letzten Auslösens pro Alarm.
// Nicht im Flash — wird bei jedem Boot zurückgesetzt.
static int last_triggered_day_for_alarm[MAX_ALARMS];

static int activeAlarmId = -1;               // -1 = kein aktiver Alarm
static unsigned long alarm_start_millis = 0; // millis()-Wert beim Alarmstart

long msToNextAlarm = -1;

// Gibt Zeiger auf das globale Alarm-Array zurück
Alarm* getAlarms() {
    return alarms;
}

// Ersetzt alle Alarme durch neue Werte und berechnet den nächsten Auslösezeitpunkt
void setAlarms(const Alarm newAlarms[MAX_ALARMS]) {
    resetAlarms();
    memcpy(alarms, newAlarms, sizeof(alarms));
    calculateMillisToNextAlarm();
}

// Gibt den Index des aktuell laufenden Alarms zurück (-1 wenn keiner aktiv)
int getActiveAlarmIndex() {
    return activeAlarmId;
}

bool isAlarmActive() {
    return activeAlarmId != -1;
}

// Aktiviert einen Alarm per Index und merkt die Startzeit; -1 deaktiviert
void setActiveAlarm(int index) {
    if(index < -1 || index >= MAX_ALARMS || activeAlarmId == index) {
        return;
    }

    activeAlarmId = index;

    if(activeAlarmId != -1) {
        TimeInfo currentTimeInfo = getCurrentTimeInfo();
        // Merken an welchem Jahrestag der Alarm ausgelöst hat, damit er heute nicht doppelt feuert
        last_triggered_day_for_alarm[activeAlarmId] = currentTimeInfo.day;
        alarm_start_millis = millis();
    }
    serialPrint("Active alarm set to: " + String(activeAlarmId));
}

// Stoppt den laufenden Alarm
void stopActiveAlarm() {
    if(activeAlarmId != -1) {
        setActiveAlarm(-1);
    }
}

// Prüft alle 10 Sekunden ob ein Alarm starten oder stoppen soll
void checkAlarmStates(uint16_t durationMinutes) {
    static unsigned long lastCheck = 0;
    unsigned long currentTime = millis();

    if(currentTime - lastCheck < 10000) {
        return;
    }
    lastCheck = currentTime;

    unsigned long runningMs = millis() - alarm_start_millis;
    TimeInfo currentTimeInfo = getCurrentTimeInfo();

    // --- Laufenden Alarm stoppen wenn Dauer überschritten ---
    if(activeAlarmId != -1 && runningMs > (unsigned long)durationMinutes * 60 * 1000) {
        serialPrint("Stopping active alarm after duration here : " + String(activeAlarmId));
        stopActiveAlarm();
    }

    // --- Alarm starten wenn Wochentag, Stunde und Minute passen ---
    for(int i = 0; i < MAX_ALARMS; i++) {
        // Überspringen wenn: anderer Alarm aktiv, Alarm deaktiviert,
        // heute schon ausgelöst, oder Zeit stimmt nicht
        if(i != activeAlarmId && alarms[i].active && currentTimeInfo.day != last_triggered_day_for_alarm[i]
           && alarms[i].day == currentTimeInfo.dayOfWeek && alarms[i].hour == currentTimeInfo.hour
           && alarms[i].minute == currentTimeInfo.minute) {
            serialPrint("Alarm[" + String(i) + "]: day=" + String(alarms[i].day) + ", hour=" + String(alarms[i].hour)
                        + ", minute=" + String(alarms[i].minute)
                        + ", last_triggered=" + String(last_triggered_day_for_alarm[i]));

            setActiveAlarm(i);
            break; // Nur einen Alarm gleichzeitig starten
        }
    }

    calculateMillisToNextAlarm();
}

// Gibt die aktuelle Alarm-Farbe zurück basierend auf dem Animations-Fortschritt (Sonnenaufgang)
bool getAlarmColor(unsigned long currentMillis, RGB& color, uint16_t durationMinutes) {
    if(activeAlarmId == -1) {
        return false;
    }

    unsigned long elapsed_millis = currentMillis - alarm_start_millis;
    const unsigned long wakeup_duration_millis = (unsigned long)durationMinutes * 60 * 1000;

    if(elapsed_millis > wakeup_duration_millis) {
        // Dauer überschritten → Endfarbe zeigen
        color = sunriseFade(1.0);
        return true;
    }

    // Fortschritt 0.0–1.0 berechnen
    float progress = (float)elapsed_millis / wakeup_duration_millis;
    color = sunriseFade(progress);
    return true;
}

// Gibt die interpolierte Helligkeit zurück: startet bei 1, steigt auf 7 in den ersten 70% der Alarm-Dauer
byte getAlarmBrightness(uint16_t durationMinutes) {
    if(activeAlarmId == -1) {
        return 0;
    }
    unsigned long elapsed_millis = millis() - alarm_start_millis;
    const unsigned long total_duration_millis = (unsigned long)durationMinutes * 60 * 1000;
    // Helligkeit steigt nur in den ersten 70% der Dauer
    const unsigned long brightness_duration_millis = (total_duration_millis * 70) / 100;
    if(elapsed_millis >= brightness_duration_millis) {
        return 7;
    }
    float progress = (float)elapsed_millis / brightness_duration_millis;
    byte brightness = 1 + (byte)(progress * (7 - 1));
    serialPrint("Alarm brightness calculated: " + String(brightness) + " (progress: " + String(progress) + ")");
    return brightness;
}

// Setzt alle Alarm-Zustände zurück (kein Alarm aktiv, Auslösetage geleert)
void resetAlarms() {
    serialPrint("Resetting all alarms and active states");
    stopActiveAlarm();
    for(int i = 0; i < MAX_ALARMS; ++i) {
        last_triggered_day_for_alarm[i] = -1;
    }
    activeAlarmId = -1;
    alarm_start_millis = 0;
}

// Liest aktuelle Systemzeit und gibt Stunde, Minute, Wochentag und Jahrestag zurück
TimeInfo getCurrentTimeInfo() {
    time_t now;
    time(&now);
    struct tm* ptm = localtime(&now);
    TimeInfo info;
    info.hour = ptm->tm_hour;
    info.minute = ptm->tm_min;
    // tm_wday: 0=Sonntag → umrechnen auf 1=Montag bis 7=Sonntag
    info.dayOfWeek = ptm->tm_wday == 0 ? 7 : ptm->tm_wday;
    info.day = ptm->tm_yday;
    return info;
}

// Berechnet Millisekunden bis zum nächsten Alarm und speichert in msToNextAlarm
// Berücksichtigt den Fall dass der Alarm in dieser Woche schon vorbei ist → nächste Woche
void calculateMillisToNextAlarm() {
    if(isAlarmActive()) {
        msToNextAlarm = 0;
        return;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t now = tv.tv_sec;
    struct tm* ptm = localtime(&now);

    long minMillis = -1;
    for(int i = 0; i < MAX_ALARMS; i++) {
        const Alarm& alarm = alarms[i];
        if(!alarm.active || alarm.day == 0)
            continue;

        struct tm target = *ptm;
        target.tm_hour = alarm.hour;
        target.tm_min = alarm.minute;
        target.tm_sec = 0;

        // Tage bis zum nächsten Auslösetag berechnen (0 = heute)
        int currentDoW = ptm->tm_wday == 0 ? 7 : ptm->tm_wday;
        int daysDelta = alarm.day - currentDoW;
        if(daysDelta < 0)
            daysDelta += 7;

        target.tm_mday += daysDelta;
        time_t alarmEpoch = mktime(&target);

        // Wenn Alarmzeit heute bereits vorbei ist → auf nächste Woche schieben
        if(alarmEpoch <= now) {
            target.tm_mday += 7;
            alarmEpoch = mktime(&target);
        }

        long diffMs = (alarmEpoch - now) * 1000L - (tv.tv_usec / 1000);

        if(minMillis == -1 || diffMs < minMillis) {
            minMillis = diffMs;
        }
    }
    msToNextAlarm = minMillis;
}

// Gibt den berechneten Wert zurück (-1 = kein Alarm gesetzt, 0 = Alarm läuft gerade)
long getMillisToNextAlarm() {
    return msToNextAlarm;
}

// Prüft ob mindestens ein Alarm aktiv und mit Wochentag gesetzt ist
bool hasActiveAlarms() {
    for(int i = 0; i < MAX_ALARMS; i++) {
        if(alarms[i].active && alarms[i].day != 0) {
            return true;
        }
    }
    return false;
}
