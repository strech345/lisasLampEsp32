#include "celebration.h"
#include <time.h>
#include "debug_utils.h"

struct CelebrationDate {
    int month;
    int day;
};

static const CelebrationDate celebrationDates[] = {
    {1, 1},  // Jan 1
    {8, 8},  // Aug 8
    {9, 2},  // Sep 2
    {12, 24} // Dec 24
};

static bool celebrationActive = false;
static int lastTriggeredDay = -1; // store tm_yday to prevent re-triggering if stopped manually

static bool isCelebrationDate(int m, int d) {
    for(const auto& date : celebrationDates) {
        if(date.month == m && date.day == d)
            return true;
    }
    return false;
}

void checkCelebrationLogic() {
    time_t nowTime;
    time(&nowTime);
    struct tm* ptm = localtime(&nowTime);

    // If day changed, reset the suppressed trigger
    if(lastTriggeredDay != -1 && lastTriggeredDay != ptm->tm_yday) {
        lastTriggeredDay = -1;
    }

    if(isCelebrationDate(ptm->tm_mon + 1, ptm->tm_mday)) {
        // Trigger at 7:00 AM if not already running and not manually stopped today
        if(ptm->tm_hour >= 7 && !celebrationActive && lastTriggeredDay == -1) {
            serialPrint("It's a celebration day! Starting celebration mode at 7 AM.");
            celebrationActive = true;
            lastTriggeredDay = ptm->tm_yday;
        }
    } else {
        // If the day is no longer a celebration day, ensure it's stopped
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

long getMillisToNextCelebration() {
    time_t nowSecs;
    time(&nowSecs);
    struct tm* ptm = localtime(&nowSecs);

    long minMillis = -1;

    for(const auto& date : celebrationDates) {
        struct tm target = *ptm;
        target.tm_mon = date.month - 1;
        target.tm_mday = date.day;
        target.tm_hour = 7;
        target.tm_min = 0;
        target.tm_sec = 0;

        time_t celebrationEpoch = mktime(&target);

        // If this celebration is today
        if(target.tm_mon == ptm->tm_mon && target.tm_mday == ptm->tm_mday) {
            // If already passed 7 AM or manually stopped today, skip to next year
            if(ptm->tm_hour >= 7 || lastTriggeredDay == ptm->tm_yday) {
                target.tm_year += 1;
                celebrationEpoch = mktime(&target);
            }
        }
        // If the date has already passed this year, look at next year
        else if(celebrationEpoch < nowSecs) {
            target.tm_year += 1;
            celebrationEpoch = mktime(&target);
        }

        long diffMs = (long)(celebrationEpoch - nowSecs) * 1000L;
        if(minMillis == -1 || diffMs < minMillis) {
            minMillis = diffMs;
        }
    }

    return minMillis;
}
