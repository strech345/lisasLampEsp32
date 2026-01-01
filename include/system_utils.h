#ifndef SYSTEM_UTILS_H
#define SYSTEM_UTILS_H

/**
 * Returns true if intervalMs has passed since *lastRunTime, and updates
 * *lastRunTime.
 */
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs);

enum ProcessStatus {
    PROCESS_INACTIVE,  // The timer is not running
    PROCESS_RUNNING,   // The timer is active and running (not timed out yet)
    PROCESS_TIMED_OUT, // The timer has expired
    PROCESS_STARTING
};
ProcessStatus getTimeoutState(unsigned long* pTimerMs, const bool startSignal,
                              const unsigned long durationMs);

bool startOnce(bool* pHasStarted, const bool startSignal);
#endif // SYSTEM_UTILS_H
