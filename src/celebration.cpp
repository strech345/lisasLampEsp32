// Feier-Modus — Implementierung
// Manuell gestoppter Modus wird für den Rest des Tages unterdrückt (Starttag im RTC-Speicher).
#include "celebration.h"
#include <time.h>
#include <esp_attr.h>
#include "debug_utils.h"

// Party-Tage aus den Systemeinstellungen (in der Weboberfläche änderbar, Standard aus secrets.h)
static const SystemSettings* s_settings = nullptr;

// Merkt sich die Systemeinstellungen mit den Party-Tagen (einmal in setup())
void initCelebration(const SystemSettings* settings) {
    s_settings = settings;
}

static bool celebrationActive = false;

// Tag des letzten Starts als Jahr * 1000 + Jahrestag (tm_yday); -1 = heute noch nicht gestartet.
// Verhindert einen Neustart nach manuellem Stopp — im RTC-Speicher, damit das auch nach Deep Sleep gilt
// (sonst startete z. B. ein Timer-Wakeup um 23 Uhr den Regenbogen erneut), ebenso nach Neustart/Absturz.
// Nach dem Einschalten ist der Speicher zufällig → die Kennung erkennt das und setzt -1.
#define CELEBRATION_RTC_MAGIC 0xCE1EB001u
struct CelebrationRtcState {
    uint32_t magic;
    int32_t lastTriggeredDay;
};
RTC_NOINIT_ATTR static CelebrationRtcState s_rtc;

// Liefert den gemerkten Starttag; nach dem Einschalten (Kennung fehlt) zuerst auf -1 setzen
static int32_t& lastTriggeredDay() {
    if(s_rtc.magic != CELEBRATION_RTC_MAGIC) {
        s_rtc.lastTriggeredDay = -1;
        s_rtc.magic = CELEBRATION_RTC_MAGIC;
    }
    return s_rtc.lastTriggeredDay;
}

// Schlüssel für "heute": Jahr * 1000 + Jahrestag (eindeutig auch über den Jahreswechsel)
static int32_t dayKey(const struct tm* t) {
    return (int32_t)(t->tm_year + 1900) * 1000 + t->tm_yday;
}

// Prüft ob Monat/Tag in der Feiertagsliste ist
static bool isCelebrationDate(int m, int d) {
    if(s_settings == nullptr)
        return false;
    for(uint8_t i = 0; i < s_settings->celebrationCount && i < MAX_CELEBRATIONS; i++) {
        if(s_settings->celebrations[i].month == m && s_settings->celebrations[i].day == d)
            return true;
    }
    return false;
}

// Prüft täglich ob ein Feiertag ist und startet den Modus um 7:00 Uhr
void checkCelebrationLogic() {
    time_t nowTime;
    time(&nowTime);
    struct tm* ptm = localtime(&nowTime);

    // Wenn der Tag gewechselt hat, Unterdrückung zurücksetzen
    const int32_t today = dayKey(ptm);
    if(lastTriggeredDay() != -1 && lastTriggeredDay() != today) {
        lastTriggeredDay() = -1;
    }

    // Systemzeit vor 2020 = nie synchronisiert (Epoch-0 = 1.1.1970 wäre sonst ein Feiertag)
    if(ptm->tm_year < (2020 - 1900)) {
        return;
    }

    if(isCelebrationDate(ptm->tm_mon + 1, ptm->tm_mday)) {
        // Feier-Modus starten wenn es 7 Uhr oder später ist und heute noch nicht gestartet wurde
        if(ptm->tm_hour >= 7 && !celebrationActive && lastTriggeredDay() == -1) {
            serialPrint("It's a celebration day! Starting celebration mode at 7 AM.");
            celebrationActive = true;
            lastTriggeredDay() = today;
        }
    } else {
        // Kein Feiertag mehr → sicherstellen dass der Modus gestoppt ist
        if(celebrationActive) {
            stopCelebration();
        }
    }
}

void stopCelebration() {
    if(celebrationActive) {
        serialPrint("Stopping celebration mode.");
        celebrationActive = false;
    }
}

bool isCelebrationActive() {
    return celebrationActive;
}

// Gibt Millisekunden bis zum nächsten Feiertag zurück (7 Uhr morgens)
// Berücksichtigt manuell gestoppten Modus: dann erst nächstes Jahr
int64_t getMillisToNextCelebration() {
    time_t nowSecs;
    time(&nowSecs);
    struct tm* ptm = localtime(&nowSecs);

    int64_t minMillis = -1;

    for(uint8_t i = 0; s_settings != nullptr && i < s_settings->celebrationCount && i < MAX_CELEBRATIONS; i++) {
        const CelebrationDay& date = s_settings->celebrations[i];
        struct tm target = *ptm;
        target.tm_mon = date.month - 1;
        target.tm_mday = date.day;
        target.tm_hour = 7;
        target.tm_min = 0;
        target.tm_sec = 0;

        time_t celebrationEpoch = mktime(&target);

        if(target.tm_mon == ptm->tm_mon && target.tm_mday == ptm->tm_mday) {
            // Feiertag ist heute: wenn 7 Uhr schon vorbei oder manuell gestoppt → nächstes Jahr
            if(ptm->tm_hour >= 7 || lastTriggeredDay() == dayKey(ptm)) {
                target.tm_year += 1;
                celebrationEpoch = mktime(&target);
            }
        } else if(celebrationEpoch < nowSecs) {
            // Datum liegt in diesem Jahr in der Vergangenheit → nächstes Jahr
            target.tm_year += 1;
            celebrationEpoch = mktime(&target);
        }

        int64_t diffMs = (int64_t)(celebrationEpoch - nowSecs) * 1000LL;
        if(minMillis == -1 || diffMs < minMillis) {
            minMillis = diffMs;
        }
    }

    return minMillis;
}
