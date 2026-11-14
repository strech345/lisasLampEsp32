#include "celebration.h"
#include <time.h>
#include "debug_utils.h"

struct CelebrationDate {
    int month;
    int day;
};

// Feiertage an denen der Feier-Modus ab 7:00 Uhr startet
static const CelebrationDate celebrationDates[] = {
    {1, 1},  // Neujahr
    {8, 8},  // Lisas Geburtstag
    {9, 2},  // Jahrestag
    {12, 24} // Heiligabend
};

static bool celebrationActive = false;
static int lastTriggeredDay = -1; // tm_yday des letzten Starts — verhindert Neustart nach manuellem Stopp

// Prüft ob Monat/Tag in der Feiertagsliste ist
static bool isCelebrationDate(int m, int d) {
    for(const auto& date : celebrationDates) {
        if(date.month == m && date.day == d)
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
    if(lastTriggeredDay != -1 && lastTriggeredDay != ptm->tm_yday) {
        lastTriggeredDay = -1;
    }

    // Systemzeit vor 2020 = nie synchronisiert (Epoch-0 = 1.1.1970 wäre sonst ein Feiertag)
    if(ptm->tm_year < (2020 - 1900)) {
        return;
    }

    if(isCelebrationDate(ptm->tm_mon + 1, ptm->tm_mday)) {
        // Feier-Modus starten wenn es 7 Uhr oder später ist und heute noch nicht gestartet wurde
        if(ptm->tm_hour >= 7 && !celebrationActive && lastTriggeredDay == -1) {
            serialPrint("It's a celebration day! Starting celebration mode at 7 AM.");
            celebrationActive = true;
            lastTriggeredDay = ptm->tm_yday;
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

    for(const auto& date : celebrationDates) {
        struct tm target = *ptm;
        target.tm_mon = date.month - 1;
        target.tm_mday = date.day;
        target.tm_hour = 7;
        target.tm_min = 0;
        target.tm_sec = 0;

        time_t celebrationEpoch = mktime(&target);

        if(target.tm_mon == ptm->tm_mon && target.tm_mday == ptm->tm_mday) {
            // Feiertag ist heute: wenn 7 Uhr schon vorbei oder manuell gestoppt → nächstes Jahr
            if(ptm->tm_hour >= 7 || lastTriggeredDay == ptm->tm_yday) {
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
