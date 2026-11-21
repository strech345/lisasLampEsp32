// Wecker-Modul — Implementierung
// Der Auslösetag (Jahr + Jahrestag) wird pro Wecker im RTC-Speicher gemerkt, damit ein Wecker pro Tag nur
// einmal feuert — auch über Deep Sleep, Neustart und Absturz hinweg (nicht über einen Stromausfall).
#include "alarm.h"
#include <string.h>
#include <time.h>
#include <esp_attr.h>
#include "debug_utils.h"

// Hilfsstruct: aktuelle Uhrzeit und Wochentag
struct TimeInfo {
    int hour;
    int minute;
    int dayOfWeek; // 1–7 (Mo–So)
    int day;       // Tag des Jahres (0–365), zur Entdopplung genutzt
    int year;      // z. B. 2026
    int month;     // 1–12
    int mday;      // 1–31
};

TimeInfo getCurrentTimeInfo();

// Zeitfenster (Minuten), in dem ein verpasster Alarm noch nachträglich auslöst
#define ALARM_GRACE_MINUTES 10

// Prüft ob die Systemzeit plausibel ist (>= 2020-01-01)
static bool isSystemTimeValid() {
    return time(nullptr) >= 1577836800L;
}

Alarm alarms[MAX_ALARMS];

// --- Alarmstatus ---

// Tag des letzten Auslösens pro Alarm als Jahr * 1000 + Jahrestag (tm_yday); -1 = noch nie.
// RTC_NOINIT: übersteht Deep Sleep (jedes Aufwachen ist ein Neustart), ESP.restart(), Watchdog und Panic.
// Sonst feuerte ein gestoppter Wecker nach einem Aufwachen im 10-min-Nachlauffenster erneut.
// Nach dem Einschalten ist der Speicher zufällig → die Kennung erkennt das und setzt alles auf -1.
#define ALARM_RTC_MAGIC 0xA1A2B001u
struct AlarmRtcState {
    uint32_t magic;
    int32_t lastTriggeredDay[MAX_ALARMS];
};
RTC_NOINIT_ATTR static AlarmRtcState s_rtc;

// Stellt den RTC-Zustand bereit: nach dem Einschalten (Kennung fehlt) alle Auslösetage leeren
static void ensureRtcState() {
    if(s_rtc.magic == ALARM_RTC_MAGIC)
        return;
    for(int i = 0; i < MAX_ALARMS; i++)
        s_rtc.lastTriggeredDay[i] = -1;
    s_rtc.magic = ALARM_RTC_MAGIC;
}

// Schlüssel für "heute": Jahr * 1000 + Jahrestag (eindeutig auch über den Jahreswechsel)
static int32_t dayKey(int year, int yday) {
    return (int32_t)year * 1000 + yday;
}

static int activeAlarmId = -1;               // -1 = kein aktiver Alarm
static unsigned long alarm_start_millis = 0; // millis()-Wert beim Alarmstart

long msToNextAlarm = -1;

// Ersetzt die Alarmliste und berechnet den nächsten Auslösezeitpunkt.
// Ein laufender Alarm und die "heute schon ausgelöst"-Tage bleiben erhalten; nur für geänderte
// Einträge (Tag/Stunde/Minute/aktiv) wird der Auslösetag zurückgesetzt, ein geänderter oder
// deaktivierter aktiver Alarm wird gestoppt. Der erste Aufruf nach dem Start (aus setup()) übernimmt
// die Liste nur: Die Auslösetage im RTC-Speicher gelten weiter.
void setAlarms(const Alarm newAlarms[MAX_ALARMS]) {
    static bool s_loaded = false; // false = erster Aufruf seit dem Start (alarms[] ist noch leer)
    ensureRtcState();
    if(!s_loaded) {
        s_loaded = true;
        memcpy(alarms, newAlarms, sizeof(alarms));
        calculateMillisToNextAlarm();
        return;
    }
    for(int i = 0; i < MAX_ALARMS; i++) {
        bool timeChanged = alarms[i].day != newAlarms[i].day || alarms[i].hour != newAlarms[i].hour
            || alarms[i].minute != newAlarms[i].minute || alarms[i].once != newAlarms[i].once
            || alarms[i].onceDay != newAlarms[i].onceDay || alarms[i].onceMonth != newAlarms[i].onceMonth
            || alarms[i].onceYear != newAlarms[i].onceYear;
        bool changed = timeChanged || alarms[i].active != newAlarms[i].active;
        if(changed) {
            s_rtc.lastTriggeredDay[i] = -1; // Eintrag geändert → darf heute wieder auslösen
        }
        // Aktiver Alarm wurde deaktiviert oder zeitlich geändert → stoppen
        if(i == activeAlarmId && (timeChanged || !newAlarms[i].active)) {
            stopActiveAlarm();
        }
    }
    memcpy(alarms, newAlarms, sizeof(alarms));
    calculateMillisToNextAlarm();
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
        s_rtc.lastTriggeredDay[activeAlarmId] = dayKey(currentTimeInfo.year, currentTimeInfo.day);
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

    // --- Alarm starten wenn heute der Wochentag passt, die Alarmzeit in den letzten 10 Minuten lag
    //     und der Alarm heute noch nicht ausgelöst wurde (robust gegen verspätetes Aufwachen/Zeitsprünge) ---
    if(!isSystemTimeValid()) {
        calculateMillisToNextAlarm();
        return; // Ohne gültige Uhrzeit nie auslösen
    }
    int nowMinutes = currentTimeInfo.hour * 60 + currentTimeInfo.minute;
    const int32_t todayKey = dayKey(currentTimeInfo.year, currentTimeInfo.day);
    for(int i = 0; i < MAX_ALARMS; i++) {
        // Überspringen wenn: anderer Alarm aktiv, Alarm deaktiviert, heute schon ausgelöst oder falscher Tag
        // (wöchentlich: Wochentag; einmalig: genaues Datum inkl. Jahr → feuert nie ein zweites Mal)
        const Alarm& a = alarms[i];
        bool today = a.once ? (a.onceYear == currentTimeInfo.year && a.onceMonth == currentTimeInfo.month
                               && a.onceDay == currentTimeInfo.mday)
                            : (a.day == currentTimeInfo.dayOfWeek);
        if(i == activeAlarmId || !a.active || !today || todayKey == s_rtc.lastTriggeredDay[i]) {
            continue;
        }
        int sinceAlarmMin = nowMinutes - (alarms[i].hour * 60 + alarms[i].minute); // Minuten seit Alarmzeit (heute)
        if(sinceAlarmMin >= 0 && sinceAlarmMin <= ALARM_GRACE_MINUTES && activeAlarmId == -1) {
            serialPrint("Alarm[" + String(i) + "]: day=" + String(alarms[i].day) + ", hour=" + String(alarms[i].hour)
                        + ", minute=" + String(alarms[i].minute)
                        + ", last_triggered=" + String(s_rtc.lastTriggeredDay[i]));

            setActiveAlarm(i);
            break; // Nur einen Alarm gleichzeitig starten
        }
    }

    calculateMillisToNextAlarm();
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
    return brightness;
}

// Setzt alle Alarm-Zustände zurück (kein Alarm aktiv, Auslösetage geleert)
void resetAlarms() {
    serialPrint("Resetting all alarms and active states");
    stopActiveAlarm();
    for(int i = 0; i < MAX_ALARMS; ++i) {
        s_rtc.lastTriggeredDay[i] = -1;
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
    info.year = ptm->tm_year + 1900;
    info.month = ptm->tm_mon + 1;
    info.mday = ptm->tm_mday;
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

    // Obergrenze: weiter entfernte Termine spielen für den Deep Sleep (max. 5 h) keine Rolle;
    // verhindert einen Überlauf von long (32 Bit, ~24 Tage in ms)
    const int64_t MAX_MS = 7LL * 24 * 60 * 60 * 1000;

    int64_t minMillis = -1;
    for(int i = 0; i < MAX_ALARMS; i++) {
        const Alarm& alarm = alarms[i];
        if(!alarm.active || (alarm.day == 0 && !alarm.once))
            continue;

        struct tm target = *ptm;
        target.tm_hour = alarm.hour;
        target.tm_min = alarm.minute;
        target.tm_sec = 0;
        target.tm_isdst = -1; // Sommer-/Winterzeit am Zieltag selbst bestimmen (sonst ggf. 1 h daneben)

        time_t alarmEpoch;
        if(alarm.once) {
            // Einmaliger Termin: festes Datum; liegt es in der Vergangenheit, zählt er nicht mehr
            target.tm_year = alarm.onceYear - 1900;
            target.tm_mon = alarm.onceMonth - 1;
            target.tm_mday = alarm.onceDay;
            alarmEpoch = mktime(&target);
            if(alarmEpoch <= now)
                continue;
        } else {
            // Wöchentlich: Tage bis zum nächsten Auslösetag (0 = heute)
            int currentDoW = ptm->tm_wday == 0 ? 7 : ptm->tm_wday;
            int daysDelta = alarm.day - currentDoW;
            if(daysDelta < 0)
                daysDelta += 7;
            target.tm_mday += daysDelta;
            alarmEpoch = mktime(&target);
            // Wenn Alarmzeit heute bereits vorbei ist → auf nächste Woche schieben
            if(alarmEpoch <= now) {
                target.tm_mday += 7;
                target.tm_isdst = -1;
                alarmEpoch = mktime(&target);
            }
        }

        int64_t diffMs = (int64_t)(alarmEpoch - now) * 1000LL - (tv.tv_usec / 1000);
        if(diffMs > MAX_MS)
            diffMs = MAX_MS;
        if(minMillis == -1 || diffMs < minMillis) {
            minMillis = diffMs;
        }
    }
    msToNextAlarm = (long)minMillis;
}

// Gibt den berechneten Wert zurück (-1 = kein Alarm gesetzt, 0 = Alarm läuft gerade)
long getMillisToNextAlarm() {
    return msToNextAlarm;
}
