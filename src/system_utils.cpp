#include <Arduino.h>
#include <system_utils.h>

// intervall, start on first run
bool isTimeForAction(unsigned long* lastRunTime, unsigned long intervalMs) {
    unsigned long currentTime = millis();

    // Check if the required time has passed using the subtraction method
    if(currentTime - *lastRunTime >= intervalMs) {
        // Update the last run time
        *lastRunTime = currentTime;
        return true; // Time to execute the action
    }

    return false; // Not yet time for the action
}

/**
 * @brief Manages a non-blocking, time-limited process.
 * * @param pTimerMs A pointer to the static unsigned long variable tracking the
 * process start time.
 * @param startSignal TRUE to start/restart the timer; FALSE to continue
 * checking.
 * @param durationMs The maximum time allowed for the process before it times
 * out.
 * @return ProcessStatus The current state of the process.
 */
ProcessStatus getTimeoutState(unsigned long* pTimerMs, const bool startSignal,
                              const unsigned long durationMs) {
    unsigned long currentTime = millis();

    // 1. START/RESTART LOGIC
    if(startSignal && *pTimerMs == 0) {
        // Reset and start the timer
        *pTimerMs = currentTime;
        return PROCESS_STARTING;
    }

    // 2. CHECK IF ACTIVE
    if(*pTimerMs == 0) {
        // Timer is not active and no start signal was given
        return PROCESS_INACTIVE;
    }

    // 3. TIMEOUT LOGIC
    if(currentTime - *pTimerMs >= durationMs) {
        // Timer expired
        *pTimerMs = 0; // Reset the timer state
        return PROCESS_TIMED_OUT;
    }

    // 4. STILL RUNNING
    return PROCESS_RUNNING;
}

bool startOnce(bool* pHasStarted, const bool startSignal) {
    if(startSignal) {
        *pHasStarted = true;
        return true; // Indicate that the process should start now
    }
    return false;
}